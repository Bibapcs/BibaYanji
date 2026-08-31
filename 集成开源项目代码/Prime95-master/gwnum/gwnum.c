/*----------------------------------------------------------------------
| gwnum.c
|
| This file contains the C routines and global variables that are used
| in the multi-precision arithmetic routines.  That is, all routines
| that deal with the gwnum data type.
| 
|  Copyright 2002-2024 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

/* Include files and a forward declaration! */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include "cpuid.h"
#include "gwnum.h"
#include "gwtables.h"
#include "gwini.h"
#include "gwutil.h"
#define DBLDBL_INLINES
#include "gwdbldbl.h"
#include "gwbench.h"
#include "radix.h"

//#define GDEBUG_MEM	1			// Print out memory used

/* Macros to point to the cyclic and negacyclic gwnums within a GENERAL_MOD combined gwnum */

#define cyclic_gwnum(h,g)	(g)
#define negacyclic_gwnum(h,g)	((gwnum) ((char *)(g) + round_up_to_multiple_of (gwnum_datasize((h)->cyclic_gwdata) + GW_HEADER_SIZE((h)->negacyclic_gwdata), 128)))

/* Option to share sin/cos data where possible amongst several gwnum callers */
/* There is little downside to enabling this option (more memory allocated) and */
/* little upside (very slight decrease in working set size and needed memory bandwidth). */

#ifndef GDEBUG_MEM
#define SHARE_SINCOS_DATA
#endif

/* Include a random number generator.  For reasons we'll discuss later we do not want to use the C runtime library's */
/* random number generator to initialize GW_RANDOM. */

#include "mt19937ar.c"

/* Global variables */

gwmutex	gwclone_lock;				/* This mutex lets parent and cloned gwdatas atomicly change a set of values */
int	gwclone_lock_initialized = FALSE;	/* Whether clone mutex is initialized */

/* When debugging gwnum and giants, I sometimes write code that "cheats" */
/* by calling a routine that is part of prime95 rather than the gwnum and */
/* giants library.  Prime95 will set this routine pointer so that gwnum */
/* code can cheat while keeping the gwnum library interface clean. */

void (*OutputBothRoutine)(int, const char *) = NULL;
#define OutputBoth(t,x)	(*OutputBothRoutine)(t,x)

/* Assembly helper routines */

struct gwinfo1_data {
	const struct gwasm_jmptab *sse2_cyclic_fft_info; /* SSE2 mersenne mod FFT info */
	const struct gwasm_jmptab *sse2_negacyclic_fft_info; /* SSE2 2^N+1 mod FFT */
	const struct gwasm_jmptab *x86_cyclic_fft_info; /* x86 mersenne mod FFT */
	const struct gwasm_jmptab *x86_negacyclic_fft_info; /* x86 2^N+1 mod FFT */
	const struct gwasm_jmptab *avx_cyclic_fft_info; /* AVX mersenne mod FFT */
	const struct gwasm_jmptab *avx_negacyclic_fft_info; /* AVX 2^N+1 mod FFT */
	const struct gwasm_jmptab *avx512_cyclic_fft_info; /* AVX mersenne mod FFT */
	const struct gwasm_jmptab *avx512_negacyclic_fft_info; /* AVX 2^N+1 mod FFT */
	uint32_t version;			/* Gwnum lib version number */
};

void gwinfo1 (struct gwinfo1_data *);
void prefetchL2 (void *addr, int count);
void pause_for_count (int count);

/* gwnum assembly routine pointers */

#define gw_fft(h,a)	(*(h)->GWPROCPTRS[0])(a)
#define gw_add(h,a)	(*(h)->GWPROCPTRS[1])(a)
#define gw_addq(h,a)	(*(h)->GWPROCPTRS[2])(a)
#define gw_addf(h,a)	(*(h)->GWPROCPTRS[2])(a)
#define gw_sub(h,a)	(*(h)->GWPROCPTRS[3])(a)
#define gw_subq(h,a)	(*(h)->GWPROCPTRS[4])(a)
#define gw_subf(h,a)	(*(h)->GWPROCPTRS[4])(a)
#define gw_addsub(h,a)	(*(h)->GWPROCPTRS[5])(a)
#define gw_addsubq(h,a)	(*(h)->GWPROCPTRS[6])(a)
#define gw_addsubf(h,a)	(*(h)->GWPROCPTRS[6])(a)
#define gw_copyzero(h,a) (*(h)->GWPROCPTRS[7])(a)
#define gw_muls(h,a)	(*(h)->GWPROCPTRS[8])(a)
#define norm_routines	9
#define zerohigh_routines 13

#define addr_gw_fft(h)		((h)->GWPROCPTRS[0])
#define addr_gw_add(h)		((h)->GWPROCPTRS[1])
#define addr_gw_addq(h)		((h)->GWPROCPTRS[2])
#define addr_gw_addf(h)		((h)->GWPROCPTRS[2])
#define addr_gw_sub(h)		((h)->GWPROCPTRS[3])
#define addr_gw_subq(h)		((h)->GWPROCPTRS[4])
#define addr_gw_subf(h)		((h)->GWPROCPTRS[4])
#define addr_gw_addsub(h)	((h)->GWPROCPTRS[5])
#define addr_gw_addsubq(h)	((h)->GWPROCPTRS[6])
#define addr_gw_addsubf(h)	((h)->GWPROCPTRS[6])
#define addr_gw_copyzero(h)	((h)->GWPROCPTRS[7])
#define addr_gw_muls(h)		((h)->GWPROCPTRS[8])
#define call_op(p,a)		(*p)(a)

/* Macro to aid in build prctabs (a prctab is an array of pointers to assembly routines) */

#define extern_decl(name)		extern void (*name)(void);
#define array_entry(name)		&name,

/* Build an array of the assembly auxiliary routines (addition, subtraction, etc.) */

#define aux_decl(name)			extern_decl(name##1)	extern_decl(name##2)
#define aux_decl3(name)			extern_decl(name##1)	extern_decl(name##2)	extern_decl(name##3)
#define aux_decl3y(name)		aux_decl3(name)	aux_decl3(name##r) aux_decl3(name##zp) aux_decl3(name##rzp) aux_decl3(name##n) aux_decl3(name##nr) aux_decl3(name##nzp) aux_decl3(name##nrzp)
#define aux_decl3z(name)		aux_decl3(name)	aux_decl3(name##r) aux_decl3(name##zp) aux_decl3(name##rzp)

#ifndef X86_64
aux_decl(gwadd)		aux_decl(gwaddq)
aux_decl(gwsub)		aux_decl(gwsubq)
aux_decl(gwaddsub)	aux_decl(gwaddsubq)
aux_decl(gwcopyzero)	aux_decl(gwmuls)

void *x87_aux_prctab[] = {
	&gwadd1, &gwaddq1, &gwsub1, &gwsubq1, &gwaddsub1, &gwaddsubq1, &gwcopyzero1, &gwmuls1,
	&gwadd2, &gwaddq2, &gwsub2, &gwsubq2, &gwaddsub2, &gwaddsubq2, &gwcopyzero2, &gwmuls2};
#endif

aux_decl3(gwxadd)	aux_decl(gwxaddq)
aux_decl3(gwxsub)	aux_decl(gwxsubq)
aux_decl3(gwxaddsub)	aux_decl(gwxaddsubq)
aux_decl(gwxcopyzero)	aux_decl3(gwxadds)	aux_decl3(gwxmuls)

void *sse2_aux_prctab[] = {
	&gwxadd1, &gwxaddq1, &gwxsub1, &gwxsubq1, &gwxaddsub1, &gwxaddsubq1, &gwxcopyzero1, &gwxmuls1,
	&gwxadd2, &gwxaddq2, &gwxsub2, &gwxsubq2, &gwxaddsub2, &gwxaddsubq2, &gwxcopyzero2, &gwxmuls2,
	&gwxadd3, &gwxaddq2, &gwxsub3, &gwxsubq2, &gwxaddsub3, &gwxaddsubq2, &gwxcopyzero2, &gwxmuls3};

aux_decl3y(gwyadd)	aux_decl3(gwyaddq)
aux_decl3y(gwysub)	aux_decl3(gwysubq)
aux_decl3y(gwyaddsub)	aux_decl3(gwyaddsubq)
aux_decl3(gwycopyzero)	aux_decl3y(gwyadds)	aux_decl3y(gwymuls)

void *avx_aux_prctab[] = {
	&gwyaddq1, &gwysubq1, &gwyaddsubq1, &gwycopyzero1,
	&gwyadd1, &gwysub1, &gwyaddsub1, &gwymuls1,
	&gwyaddr1, &gwysubr1, &gwyaddsubr1, &gwymulsr1,
	&gwyaddzp1, &gwysubzp1, &gwyaddsubzp1, &gwymulszp1,
	&gwyaddrzp1, &gwysubrzp1, &gwyaddsubrzp1, &gwymulsrzp1,
	&gwyaddn1, &gwysubn1, &gwyaddsubn1, &gwymulsn1,
	&gwyaddnr1, &gwysubnr1, &gwyaddsubnr1, &gwymulsnr1,
	&gwyaddnzp1, &gwysubnzp1, &gwyaddsubnzp1, &gwymulsnzp1,
	&gwyaddnrzp1, &gwysubnrzp1, &gwyaddsubnrzp1, &gwymulsnrzp1,

	&gwyaddq3, &gwysubq3, &gwyaddsubq3, &gwycopyzero3,
	&gwyadd3, &gwysub3, &gwyaddsub3, &gwymuls3,
	&gwyaddr3, &gwysubr3, &gwyaddsubr3, &gwymulsr3,
	&gwyaddzp3, &gwysubzp3, &gwyaddsubzp3, &gwymulszp3,
	&gwyaddrzp3, &gwysubrzp3, &gwyaddsubrzp3, &gwymulsrzp3,
	&gwyaddn3, &gwysubn3, &gwyaddsubn3, &gwymulsn3,
	&gwyaddnr3, &gwysubnr3, &gwyaddsubnr3, &gwymulsnr3,
	&gwyaddnzp3, &gwysubnzp3, &gwyaddsubnzp3, &gwymulsnzp3,
	&gwyaddnrzp3, &gwysubnrzp3, &gwyaddsubnrzp3, &gwymulsnrzp3};

aux_decl3y(ygw_carries_wpn)
void gwy3_apply_carries (void *);

void *avx_carries_prctab[] = {
	&ygw_carries_wpn3, &ygw_carries_wpnr3, &ygw_carries_wpnzp3, &ygw_carries_wpnrzp3,
	&ygw_carries_wpnn3, &ygw_carries_wpnnr3, &ygw_carries_wpnnzp3, &ygw_carries_wpnnrzp3};

#ifdef X86_64
aux_decl3z(gwzadd)	aux_decl3(gwzaddq)
aux_decl3z(gwzsub)	aux_decl3(gwzsubq)
aux_decl3z(gwzaddsub)	aux_decl3(gwzaddsubq)
aux_decl3(gwzcopyzero)	aux_decl3y(gwzadds)	aux_decl3y(gwzmuls)

void *avx512_aux_prctab[] = {
	&gwzaddq3, &gwzsubq3, &gwzaddsubq3, &gwzcopyzero3,
	&gwzadd3, &gwzsub3, &gwzaddsub3, &gwzmuls3,
	&gwzaddr3, &gwzsubr3, &gwzaddsubr3, &gwzmulsr3,
	&gwzaddzp3, &gwzsubzp3, &gwzaddsubzp3, &gwzmulszp3,
	&gwzaddrzp3, &gwzsubrzp3, &gwzaddsubrzp3, &gwzmulsrzp3};

aux_decl3z(zgw_carries_wpn)
extern_decl(zgw_carries_op_wpnzp3)
extern_decl(zgw_carries_op_wpnrzp3)
void gwz3_apply_carries (void *);

void *avx512_carries_prctab[] = {
	&zgw_carries_wpn3, &zgw_carries_wpnr3, &zgw_carries_wpnzp3, &zgw_carries_wpnrzp3, &zgw_carries_op_wpnzp3, &zgw_carries_op_wpnrzp3};
#else
#define gwz3_apply_carries(a)
#endif

/* Now we put the normalization routines in an array so we can easily pick */
/* the normalization routines to use.  There is one table for AVX-512, AVX, SSE2 and */
/* x87.  The SSE2 table has these 820 combinations: */
/*	r or i		(rational or irrational) */
/*	1 or 2 or 2AMD or 3 or 3AMD (1 pass or 2 pass or 2 pass with partial normalization - with optional AMD prefetching) */
/*	z or zp or blank (zero upper half of result or zero-padded FFT or normal FFT */
/*	e or blank	(roundoff error checking or not) */
/*	b or blank	(b > 2 or not, not used in AVX-512) */
/*	s4 or blank	(SSE4 or not) */
/*	k or blank	(k for XMM_K_HI is zero or not) */
/*	c1 or cm1 or blank (c=1, c=-1, abs(c)!=1, not used in AVX-512) */
/* We also define a macro that will pick the correct entry from the array. */

/* First is the AVX-512 table */

#ifdef X86_64
#define avx512_explode(macro)			avx512_explode1(macro,zr)		avx512_explode1(macro,zi)
#define avx512_explode1(macro,name)		avx512_explode2(macro,name##3,SKX)
#define avx512_explode2(macro,name,suff)	avx512_zero_explode(macro,name##z,suff)	avx512_explode3(macro,name,suff,notzp)	avx512_explode3(macro,name##zp,suff,zp)
#define avx512_zero_explode(macro,name,suff)	avx512_explode9##suff(macro,name)	avx512_explode9##suff(macro,name##e)
#define avx512_explode3(macro,name,suff,zp)	avx512_explode4(macro,name,suff,zp)	avx512_explode4(macro,name##e,suff,zp)
#define avx512_explode4(macro,name,suff,zp)	avx512_explode5(macro,name,suff,zp,notc) avx512_explode5(macro,name##c,suff,zp,c)
#define avx512_explode5(macro,name,suff,zp,c)	avx512_explode6(macro,name,suff,zp,c)
#define avx512_explode6(macro,name,suff,zp,c)	avx512_explode7##zp(macro,name,suff,c)
#define avx512_explode7notzp(macro,name,suff,c)	avx512_explode9##suff(macro,name)
#define avx512_explode7zp(macro,name,suff,c)	avx512_explode8##c(macro,name,suff)	avx512_explode8##c(macro,name##k,suff)
#define avx512_explode8c(macro,name,suff)	avx512_explode9##suff(macro,name)
#define avx512_explode8notc(macro,name,suff)	avx512_explode9##suff(macro,name)
#define avx512_explode9SKX(macro,name)		macro(name##SKX)

avx512_explode(extern_decl)
void *avx512_prctab[] = { avx512_explode(array_entry) NULL };

int avx512_prctab_index (gwhandle *gwdata, int z, int e, int c)
{
	int	index = 0;

	if (! gwdata->RATIONAL_FFT) index += 14;		/* Irrational FFTs are after the rational FFTs */

	if (z) {
		if (e) index += 1;
		return (index);
	}
	index += 2;
	if (! gwdata->ZERO_PADDED_FFT) {
		if (e) index += 2;
		if (c) index += 1;
	} else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
		index += 4;
		if (e) index += 4;
		if (!c) {
			if (asm_data->u.zmm.ZMM_K_HI_OVER_SMALL_BASE == 0.0) index += 1;
		} else {
			index += 2;
			if (asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_SMALL_BASE == 0.0) index += 1;
		}
	}
	return (index);
}
#endif

/* Now the AVX table */

#define avx_explode(macro)			avx_explode1(macro,yr)			avx_explode1(macro,yi)
#define avx_explode1(macro,name)		avx_explode2(macro,name##1,CORE)	avx_explode2(macro,name##1,FMA3)	avx_explode2(macro,name##3,CORE)	avx_explode2(macro,name##3,FMA3)
#define avx_explode2(macro,name,suff)		avx_zero_explode(macro,name##z,suff)	avx_explode3(macro,name,suff,notzp)	avx_explode3(macro,name##zp,suff,zp)
#define avx_zero_explode(macro,name,suff)	avx_explode9##suff(macro,name)		avx_explode9##suff(macro,name##e)
#define avx_explode3(macro,name,suff,zp)	avx_explode4(macro,name,suff,zp)	avx_explode4(macro,name##e,suff,zp)
#define avx_explode4(macro,name,suff,zp)	avx_explode5(macro,name,suff,zp,notc)	avx_explode5(macro,name##c,suff,zp,c)
#define avx_explode5(macro,name,suff,zp,c)	avx_explode6(macro,name,suff,zp,c)	avx_explode6(macro,name##b,suff,zp,c)
#define avx_explode6(macro,name,suff,zp,c)	avx_explode7##zp(macro,name,suff,c)
#define avx_explode7notzp(macro,name,suff,c)	avx_explode9##suff(macro,name)
#define avx_explode7zp(macro,name,suff,c)	avx_explode8##c(macro,name,suff)	avx_explode8##c(macro,name##k,suff)
#define avx_explode8c(macro,name,suff)		avx_explode9##suff(macro,name)
#define avx_explode8notc(macro,name,suff)	avx_explode9##suff(macro,name)		avx_explode9##suff(macro,name##c1)	avx_explode9##suff(macro,name##cm1)
#define avx_explode9BLEND(macro,name)		macro(name##BLEND)
#define avx_explode9CORE(macro,name)		macro(name##CORE)
#ifdef X86_64
#define avx_explode9FMA3(macro,name)		macro(name##FMA3)
#else
#define avx_explode9FMA3(macro,name)		macro(name##CORE)			/* We don't support FMA FFTs on 32-bit builds */
#endif

avx_explode(extern_decl)
void *avx_prctab[] = { avx_explode(array_entry) NULL };

int avx_prctab_index (gwhandle *gwdata, int z, int e, int c)
{
	int	index = 0;

	if (! gwdata->RATIONAL_FFT) index += 168;		/* Irrational FFTs are after the 4 sets of rational FFTs */
	if (gwdata->PASS2_SIZE) index += 84;			/* Two-pass FFTs are after the one-pass FFTs */
	if (gwdata->cpu_flags & CPU_FMA3) index += 42;		/* FMA3 FFTs are after the CORE FFTs */

	if (z) {
		if (e) index += 1;
		return (index);
	}
	index += 2;
	if (! gwdata->ZERO_PADDED_FFT) {
		if (e) index += 4;
		if (c) index += 2;
		if (gwdata->b > 2) index += 1;
	} else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
		index += 8;
		if (e) index += 16;
		if (!c) {
			if (gwdata->b > 2) index += 6;
			if (asm_data->u.ymm.YMM_K_HI[0] == 0.0) index += 3;
			if (gwdata->c == 1) index += 1;  if (gwdata->c == -1) index += 2;
		} else {
			index += 12;
			if (gwdata->b > 2) index += 2;
			if (asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[0] == 0.0) index += 1;
		}
	}
	return (index);
}				    

/* Now the SSE2 table */

#define sse2_explode(macro)			sse2_explode1(macro,xr)			sse2_explode1(macro,xi)
#define sse2_explode1(macro,name)		sse2_explode2(macro,name##1,BLEND)	sse2_explode2(macro,name##2,CORE)	sse2_explode2(macro,name##2,K8)		sse2_explode2(macro,name##3,CORE)	sse2_explode2(macro,name##3,K8)
#define sse2_explode2(macro,name,suff)		sse2_zero_explode(macro,name##z,suff)	sse2_explode3(macro,name,suff,notzp)	sse2_explode3(macro,name##zp,suff,zp)
#define sse2_zero_explode(macro,name,suff)	sse2_explode9##suff(macro,name)		sse2_explode9##suff(macro,name##e)
#define sse2_explode3(macro,name,suff,zp)	sse2_explode4(macro,name,suff,zp)	sse2_explode4(macro,name##e,suff,zp)
#define sse2_explode4(macro,name,suff,zp)	sse2_explode5(macro,name,suff,zp,notc)	sse2_explode5(macro,name##c,suff,zp,c)
#define sse2_explode5(macro,name,suff,zp,c)	sse2_explode6(macro,name,suff,zp,c)	sse2_explode6(macro,name##b,suff,zp,c)
#define sse2_explode6(macro,name,suff,zp,c)	sse2_explode7##zp(macro,name,suff,c)	sse2_explode7##zp(macro,name##s4,suff,c)
#define sse2_explode7notzp(macro,name,suff,c)	sse2_explode9##suff(macro,name)
#define sse2_explode7zp(macro,name,suff,c)	sse2_explode8##c(macro,name,suff)	sse2_explode8##c(macro,name##k,suff)
#define sse2_explode8c(macro,name,suff)		sse2_explode9##suff(macro,name)
#define sse2_explode8notc(macro,name,suff)	sse2_explode9##suff(macro,name)		sse2_explode9##suff(macro,name##c1)	sse2_explode9##suff(macro,name##cm1)
#define sse2_explode9BLEND(macro,name)		macro(name##BLEND)
#define sse2_explode9CORE(macro,name)		macro(name##CORE)
#ifndef __APPLE__
#define sse2_explode9K8(macro,name)		macro(name##K8)
#else
#define sse2_explode9K8(macro,name)		macro(name##CORE)		/* Macs do not have AMD CPUs */
#endif

sse2_explode(extern_decl)
void *sse2_prctab[] = { sse2_explode(array_entry) NULL };

int sse2_prctab_index (gwhandle *gwdata, int z, int e, int c)
{
	int	index = 0;

	if (! gwdata->RATIONAL_FFT) index += 410;
	if (gwdata->PASS2_SIZE) {
		index += 82;
		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) index += 164;
		if (gwdata->cpu_flags & CPU_3DNOW_PREFETCH) index += 82;
	}
	if (z) {
		if (e) index += 1;
		return (index);
	}
	index += 2;
	if (! gwdata->ZERO_PADDED_FFT) {
		if (e) index += 8;
		if (c) index += 4;
		if (gwdata->b > 2) index += 2;
		if (gwdata->cpu_flags & CPU_SSE41) index += 1;
	} else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
		index += 16;
		if (e) index += 32;
		if (!c) {
			if (gwdata->b > 2) index += 12;
			if (gwdata->cpu_flags & CPU_SSE41) index += 6;
			if (asm_data->u.xmm.XMM_K_HI[0] == 0.0) index += 3;
			if (gwdata->c == 1) index += 1;  if (gwdata->c == -1) index += 2;
		} else {
			index += 24;
			if (gwdata->b > 2) index += 4;
			if (gwdata->cpu_flags & CPU_SSE41) index += 2;
			if (asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[0] == 0.0) index += 1;
		}
	}
	return (index);
}				    

/* The x87 normalization routines array has 40 combinations: */
/*	r or i		(rational or irrational) */
/*	1 or 2		(1 or 2 pass FFTs) */
/*	z or zp or blank (zero upper half of result or zero-padded FFT or normal FFT */
/*	e or blank	(roundoff error checking or not) */
/*	c or blank	(mul by small const or not)  */
/* We also define a macro that will pick the correct entry from the array. */

#ifndef X86_64
#define x87_explode(macro)		x87_explode1(macro,r)		x87_explode1(macro,i)
#define x87_explode1(macro,name)	x87_explode2(macro,name##1)	x87_explode2(macro,name##2)
#define x87_explode2(macro,name)	x87_zero_explode(macro,name##z)	x87_explode3(macro,name)	x87_explode3(macro,name##zp)
#define x87_zero_explode(macro,name)	macro(name)			macro(name##e)
#define x87_explode3(macro,name)	x87_explode4(macro,name)	x87_explode4(macro,name##e)
#define x87_explode4(macro,name)	macro(name)			macro(name##c)

x87_explode(extern_decl)
void *x87_prctab[] = { x87_explode(array_entry) NULL };

#define	x87_prctab_index(gwdata, z, e, c)  \
	    ((gwdata)->RATIONAL_FFT ? 0 : 20) + \
	    ((gwdata)->PASS2_SIZE ? 10 : 0) + \
	    (z ? (e ? 1 : 0) : 2 + \
	     ((gwdata)->ZERO_PADDED_FFT ? 4 : 0) + \
	     (e ? 2 : 0) + \
	     (c ? 1 : 0))
#endif

/* Helper macros */

// These macros were required before the C99 standard
//#ifndef isinf
//#define isinf(x)		((x) != 0.0 && (x) == 2.0*(x))
//#endif
//#ifndef isnan
//#define isnan(x)		((x) != (x))
//#endif
//#define is_valid_double(x)	(! isnan (x) && ! isinf (x))

// Instead, I'm using a home grown solution that seems to be faster using isfinite macro.
// From the internet describing layout of a double:
// If sign = 0 && exponent == 11111111111 && mantissa == 0   =>   +Infinity
// If sign = 1 && exponent == 11111111111 && mantissa == 0   =>   -Infinity
// If             exponent == 11111111111 && mantissa != 0   =>   NaN
// If             exponent != 11111111111                    =>   Finite
inline int is_valid_double (double x) {
	union uint64_double { uint64_t x64; double xdbl; } xcast;
	xcast.xdbl = x;
	return ((xcast.x64 & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL);
}
#define is_valid_double_addr(x)	(((* (uint64_t *) x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL)


/* More #defines */

#define	GWINIT_WAS_CALLED_VALUE	0xAA12BB34	/* Special value checked by gwsetup to ensure gwinit was called */
#define GWFREEABLE		0x80000000	/* Flag set in the gwnum freeable field indicating aligned_free will work */
#define GWFREE_INTERNAL		0x40000000	/* Flag set in the gwnum freeable field indicating gwfreeall should not free this gwnum */
#define GWFREE_LARGE_PAGES	0x20000000	/* Flag set if gwnum was allocated using large pages */
#define GWFREEALLOC_INDEX	0x1FFFFFFF	/* Remainder of the freeable field -- index into the gwnum_alloc array */

/* Forward declarations */

int convert_giant_to_k2ncd (
	giant	g,		/* Giant to examine */
	double	*k,		/* K in (K*2^N+C)/D. */
	unsigned long *n,	/* N in (K*2^N+C)/D. */
	signed long *c,		/* C in (K*2^N+C)/D. */
	unsigned long *d);	/* D in (K*2^N+C)/D. */
int internal_gwsetup (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c);		/* C in K*B^N+C. Must be rel. prime to K. */
void raw_gwsetaddin (gwhandle *gwdata, unsigned long word, double *ptr, double val);
int multithread_init (gwhandle *gwdata);
void multithread_term (gwhandle *gwdata);
void do_multithread_op_work (gwhandle *gwdata, struct gwasm_data *asm_data);
void pass1_aux_entry_point (void*);
void pass2_aux_entry_point (void*);
void gw_fixed_random_number (gwhandle *gwdata, gwnum x);
gwnum gwalloc_internal (gwhandle *gwdata);

/* Routine to split a r4dwpn FFT word into column and group multiplier indexes. */
/* We remove bit(s) associated with the upper SSE2/AVX words because those are handled by the group multipliers. */

unsigned long dwpn_col (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long word)
{
	unsigned long high, low;

	ASSERTG (! (gwdata->cpu_flags & CPU_AVX512F));	// Except for ADDIN_VALUE, AVX-512 FFT data is normalized by the time most C code sees it

	if (gwdata->cpu_flags & CPU_AVX) {
		low = word % gwdata->PASS2_SIZE;
		high = (((word / gwdata->PASS2_SIZE) & ~3) % gwdata->wpn_count) * gwdata->PASS2_SIZE;
	} else {
		unsigned long upper_sse2_word = gwdata->PASS2_SIZE >> 1;
		high = word / upper_sse2_word;
		low = word - high * upper_sse2_word;

		high = high >> 1;
		high = high % gwdata->wpn_count;
		high = high * gwdata->PASS2_SIZE;
	}
	return (high + low);
}

/* Return true if half of the words would have the same pattern of big */
/* and little words.  */

int match_pathological_pattern (
	unsigned long num_big_words,
	unsigned long total_length,
	double	pathological_fraction)
{
	double	half_length;
	unsigned long pathological_base, actual_base;

/* Compute the gwfft_base you would get of the word half way into the FFT if */
/* you had the pathological fraction of big and little words */

	half_length = (double) total_length * 0.5;
	pathological_base = (unsigned long) ceil (half_length * pathological_fraction);

/* Compute the base you would get given the actual fraction of big words */

	actual_base = (unsigned long) ceil (half_length * (double) num_big_words / (double) total_length);

/* Return pathological (true) if the actual_base is close to the pathological_base */

	return (actual_base >= pathological_base && actual_base <= pathological_base + 1);
}

/* Here is a particularly nasty routine.  It tries to detect whether the distribution */
/* of big and little words is "pathological".  We want the distribution to be random. */
/* If, for example, there are an equal number of big words and little words then the */
/* every other FFT word consists of big word * big word products, while the other half */
/* contains big word * small word products.  This greatly increases the round off error */
/* especially when b is large (big words are much larger than small words).  This */
/* ugliness was added to handle these cases that where the wrong FFT length was selected: */
/* 211*210^2047-1, 211*210^2687-1, 211*210^7679-1.  There are undoubtedly many others. */

int is_pathological_distribution (
	unsigned long num_big_words,
	unsigned long num_small_words)
{
	unsigned long total_length;

/* Handle cases that we really should never see (rational FFTs) */

	if (num_big_words == 0 || num_small_words == 0) return (FALSE);

/* While the remaining number of big words and small words is even, this */
/* represents a case of a big repeating pattern (the pattern in the upper half */
/* of the remaining words is the same as the pattern in the lower half). */

	total_length = num_big_words + num_small_words;
	while ((num_big_words & 1) == 0 && (total_length & 1) == 0) {
		num_big_words >>= 1;
		total_length >>= 1;
	}

/* The bad patterns occur when the number of big words divided by the FFT length */
/* is close to a small rational number like 1/2, 2/5, 3/4, etc.	 We'll define a */
/* pathological bit pattern as one where more than half of the FFT repeats the */
/* same cycle of big words and small words.  This definition may require some */
/* tweaking over time. */

	if (match_pathological_pattern (num_big_words, total_length, 1.0 / 2.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 1.0 / 4.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 3.0 / 4.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 1.0 / 8.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 3.0 / 8.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 5.0 / 8.0)) return (TRUE);
	if (match_pathological_pattern (num_big_words, total_length, 7.0 / 8.0)) return (TRUE);
	if (total_length % 3 == 0) {
		if (match_pathological_pattern (num_big_words, total_length, 1.0 / 3.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 2.0 / 3.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 1.0 / 6.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 5.0 / 6.0)) return (TRUE);
	}
	if (total_length % 5 == 0) {
		if (match_pathological_pattern (num_big_words, total_length, 1.0 / 5.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 2.0 / 5.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 3.0 / 5.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 4.0 / 5.0)) return (TRUE);
	}
	if (total_length % 7 == 0) {
		if (match_pathological_pattern (num_big_words, total_length, 1.0 / 7.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 2.0 / 7.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 3.0 / 7.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 4.0 / 7.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 5.0 / 7.0)) return (TRUE);
		if (match_pathological_pattern (num_big_words, total_length, 6.0 / 7.0)) return (TRUE);
	}

/* That's all the cases we test for now */

	return (FALSE);
}

/* Determine the "bif" (best-mplementation-for) value we will look for.  Often this is a straight-forward mapping from */
/* the CPU_ARCHITECTURE.  However, for some CPU architectures, like Pentium M and Core Solo, we */
/* don't have jmptable entries detailing the fastest FFT implementations for those architectures. */

/* BIF values copied from mult.asm */
#define BIF_CORE2		0	// Core 2 CPUs, big L2 caches
#define BIF_CORE2_512		1	// Core 2 Celerons, 512K L2 cache, no L3 cache
#define BIF_I7			2	// Core i3/5/7 CPUs, 256K L2, big L3 caches
#define BIF_FMA3		3	// Core i3/5/7 CPUs - Haswell architecture with FMA3 support
#define BIF_P4_1024		4	// Pentium 4, 1MB cache
#define BIF_P4TP_512		5	// Pentium 4, 512K cache (did not support EMT64)
#define BIF_P4TP_256		6	// Pentium 4, 256K cache (did not support EMT64)
#define BIF_SKX			7	// Intel CPUs that support AVX-512 (Skylake-X)
#define BIF_K8			8	// AMD K8 CPUs
#define BIF_K10			9	// AMD K10 CPUs
#define BIF_RYZEN		11	// AMD Ryzen CPUs

/* Architecture values copied from mult.asm */
#define ARCH_P4TP		1	/* Ancient CPU - architecture value should be retired */
#define ARCH_P4			2
#define ARCH_CORE		3
#define ARCH_FMA3		4
#define ARCH_K8			5
#define ARCH_K10		6
#define ARCH_SKX		8
#define ARCH_BLEND		0

int calculate_bif (
	gwhandle *gwdata,	/* Gwnum global data */
	unsigned long fftlen,
	int	negacyclic)
{
	int	cpu_arch, retval;

/* If the architecture and CPU flags are inconsistent, correct the architecture.  This shouldn't */
/* ever happen unless the results from CPUID are overridden. */

	cpu_arch = CPU_ARCHITECTURE;
	if (cpu_arch == CPU_ARCHITECTURE_AMD_K8 && ! (gwdata->cpu_flags & CPU_3DNOW_PREFETCH))
		cpu_arch = CPU_ARCHITECTURE_CORE_2;
	if (cpu_arch == CPU_ARCHITECTURE_AMD_K10 && ! (gwdata->cpu_flags & CPU_3DNOW_PREFETCH))
		cpu_arch = CPU_ARCHITECTURE_CORE_2;
	if (cpu_arch == CPU_ARCHITECTURE_AMD_BULLDOZER && ! (gwdata->cpu_flags & CPU_3DNOW_PREFETCH))
		cpu_arch = CPU_ARCHITECTURE_CORE_2;

/* Map the CPU architecture as determined by CPUID to one of the CPU architectures */
/* that the FFT assembly code is optimized for. */

	switch (cpu_arch) {
	case CPU_ARCHITECTURE_PENTIUM_M:	/* Not sure what is best for these three architectures */
	case CPU_ARCHITECTURE_CORE:
	case CPU_ARCHITECTURE_ATOM:
		retval = BIF_P4_1024;		/* Look for FFTs optimized for large cache P4s */
		break;
	case CPU_ARCHITECTURE_PENTIUM_4:
		if (CPU_L2_CACHE_SIZE <= 128)	/* We haven't optimized for these yet */
			retval = BIF_P4TP_256;	/* Look for FFTs optimized for 256K cache P4s */
		else if (CPU_L2_CACHE_SIZE <= 256)
			retval = BIF_P4TP_256;	/* Look for FFTs optimized for 256K cache P4s */
		else if (CPU_L2_CACHE_SIZE <= 512)
			retval = BIF_P4TP_512;	/* Look for FFTs optimized for 512K cache P4s */
		else if (gwdata->cpu_flags & CPU_TLB_PRIMING)
			retval = BIF_P4TP_512;	/* Look for FFTs optimized for 512K cache P4s */
		else
			retval = BIF_P4_1024;	/* Look for FFTs optimized for large cache P4s */
		break;
	case CPU_ARCHITECTURE_CORE_2:
		if (CPU_L2_CACHE_SIZE <= 1024)
			retval = BIF_CORE2_512;	/* Look for FFTs optimized for Core 2 Celerons */
		else
			retval = BIF_CORE2;	/* Look for FFTs optimized for Core 2 */
		break;
	case CPU_ARCHITECTURE_CORE_I7:
		if (gwdata->cpu_flags & CPU_AVX512F)
			retval = BIF_SKX;	/* Look for Intel-optimized AVX-512 FFT. */
		else if (gwdata->cpu_flags & CPU_FMA3)
			retval = BIF_FMA3;	/* Look for FFTs optimized for Haswell CPUs with FMA3 support */
		else
			retval = BIF_I7;	/* Look for FFTs optimized for Core i3/i5/i7 */
		break;
	case CPU_ARCHITECTURE_PHI:		/* Intel's Xeon Phi CPUs */
		if (gwdata->cpu_flags & CPU_AVX512F)
			retval = BIF_SKX;	/* Look for Intel-optimized AVX-512 FFT. */
		else if (gwdata->cpu_flags & CPU_FMA3)
			retval = BIF_FMA3;	/* Look for FFTs optimized for Haswell CPUs with FMA3 support */
		else
			retval = BIF_I7;	/* Look for FFTs optimized for Core i3/i5/i7 */
		break;
	case CPU_ARCHITECTURE_INTEL_OTHER:	/* This is probably one of Intel's next generation CPUs */
		if (gwdata->cpu_flags & CPU_AVX512F)
			retval = BIF_SKX;	/* We don't know which AVX-512 FFT is fastest.  Try this one. */
		else if (gwdata->cpu_flags & CPU_FMA3)
			retval = BIF_FMA3;	/* Look for FFTs optimized for Haswell CPUs with FMA3 support */
		else
			retval = BIF_I7;	/* Look for FFTs optimized for Core i3/i5/i7 */
		break;
	case CPU_ARCHITECTURE_AMD_K8:
		retval = BIF_K8;		/* Look for FFTs optimized for K8 */
		break;
	case CPU_ARCHITECTURE_AMD_K10:
		retval = BIF_K10;		/* Look for FFTs optimized for K10 */
		break;
	case CPU_ARCHITECTURE_AMD_BULLDOZER:	/* Bulldozer is horrible at AVX.  Gwinit turns off AVX & FMA3 to get K10 optimized. */
		if (gwdata->cpu_flags & CPU_FMA3)  /* Should only happen during torture test */
			retval = BIF_FMA3;	/* FMA3 FFTs */
		else if (gwdata->cpu_flags & CPU_AVX)  /* Should only happen during torture test */
			retval = BIF_I7;	/* AVX without FMA3 FFTs */
		else
			retval = BIF_K10;	/* Look for FFTs optimized for K10 */
		break;
	case CPU_ARCHITECTURE_AMD_ZEN:		/* Look for FFTs optimized for Ryzen */
	case CPU_ARCHITECTURE_AMD_OTHER:	/* For no particularly good reason, assume future AMD processors do well with Ryzen FFTs */
		if (gwdata->cpu_flags & CPU_AVX512F)
			retval = BIF_RYZEN;	/* Use AVX-512 FFTs optimized for Ryzen chip */
		else if (gwdata->cpu_flags & CPU_FMA3)
			retval = BIF_RYZEN;
		else
			retval = BIF_I7;	/* Shouldn't happen */
		break;
	case CPU_ARCHITECTURE_OTHER:		/* Probably a VIA processor */
		if (gwdata->cpu_flags & CPU_AVX512F)
			retval = BIF_SKX;	/* We don't know which AVX-512 FFT is fastest.  Try this one. */
		else if (gwdata->cpu_flags & CPU_FMA3)
			retval = BIF_FMA3;	/* Look for FFTs optimized for Haswell CPUs with FMA3 support */
		else if (gwdata->cpu_flags & CPU_AVX)
			retval = BIF_I7;	/* Look for FFTs optimized for Core i7 (AVX without FMA3) */
		else
			retval = BIF_CORE2;	/* We don't know which FFT is fastest.  For no particularly good reason, use Core 2 FFTs */
		break;
	case CPU_ARCHITECTURE_PRE_SSE2:		/* Cannot happen, gwinfo should have selected x87 FFTs */
	default:				/* For no particularly good reason, look for FFTs optimized for Core 2 */
		retval = BIF_CORE2;
		break;
	}

/* We never implemented FMA3 FFTs for 32-bit builds (lack of YMM registers). */

#ifndef X86_64
	if (retval == BIF_FMA3 || retval == BIF_RYZEN) retval = BIF_I7;
#endif

/* For slower CPU architectures we didn't bother to find the best FFT implementation for the larger FFTs.  This was done to reduce the size of the executable. */
/* If we are asked to run one of these large FFTs, select an FFT optimized for a different CPU architecture.  GW 2023-08-05: More FFTs removed for old CPUs. */

	if (retval == BIF_P4TP_256)
		retval = BIF_P4TP_512;	/* Tiny cache P4s (Willamette) now default to FFTs optimized for larger cache Pentium 4s */
	if (fftlen > 4194304 && retval == BIF_P4TP_512)
		retval = BIF_P4_1024;	/* Small cache P4s have best FFT implementations up to 4M */
	if (fftlen > 6291456 && retval == BIF_P4_1024)
		retval = BIF_CORE2;	/* P4s have best FFT implementations up to 6M */
	if (fftlen > 6291456 && retval == BIF_K8)
		retval = BIF_K10;	/* K8s have best FFT implementations up to 6M */
	if (fftlen > 4194304 && retval == BIF_CORE2_512)
		retval = BIF_CORE2;	/* Small cache Core 2 Celerons have best FFT implementations up to 4M */
	if (fftlen > 7864320 && (negacyclic || fftlen < 18874368 || fftlen > 19660800) && (retval == BIF_CORE2 || retval == BIF_K10))
		retval = BIF_I7;	/* Core 2 and K10 have best FFT implementations up to 7680K and cyclic FFTs 18M to 19200K */

/* Return the result */

	return (retval);
}

/* Ugly little macros to bump jmptable pointer to next procedure entry or to next count */
static __inline const struct gwasm_jmptab *INC_JMPTAB (const struct gwasm_jmptab *x)
{
	if (x->flags & 0x40000000) return ((const struct gwasm_jmptab *) ((const char *)(x) + sizeof(uint32_t) + 2*sizeof(void*) + sizeof(uint32_t)));
	return ((const struct gwasm_jmptab *) ((const char *)(x) + sizeof(uint32_t) + sizeof(void*) + sizeof(uint32_t)));
}
static __inline const struct gwasm_jmptab *LAST_JMPTAB (const struct gwasm_jmptab *x)
{
	const struct gwasm_jmptab *next;
	for ( ; (next = INC_JMPTAB (x))->flags & 0x80000000; x = next);
	return (x);
}
static __inline const struct gwasm_jmptab *NEXT_SET_OF_JMPTABS (const struct gwasm_jmptab *x)
{
	// Handle case where there are no FFT implementations
	if (x->flags == 0) return ((const struct gwasm_jmptab *) &x->proc_ptr);
	// Get pointer to the last FFT implementation
	x = LAST_JMPTAB (x);
	// Adjust for jmptab containing two proc ptrs
	if (x->flags & 0x40000000) x = (const struct gwasm_jmptab *) ((const char *)(x) + sizeof(void*));
	// Skip non-zero counts
	while (x->counts[0]) x = (const struct gwasm_jmptab *) ((const char *)(x) + sizeof(int32_t));
	// Next set of jmptabs is after the zero count
	return ((const struct gwasm_jmptab *) &x->counts[1]);
}

/* This routine checks to see if there is an FFT implementation for this FFT length and */
/* CPU architecture.  For example, when the FFT length is just less than a power of two, on */
/* some CPUs it may be better to use the larger power-of-two FFT length and thus there */
/* will not be an FFT implementation for this slightly smaller FFT length. */ 

int is_fft_implemented (
	gwhandle *gwdata,		/* Gwnum global data */
	int	negacyclic,		/* TRUE if this jmptab entry if from the negacyclic FFT table */
	const struct gwasm_jmptab *jmptab, /* Jmptable entry from mult.asm to examine */
	int	*best_impl_id)		/* Returned ID of the best FFT implementation as determined by the benchmark database */
{
	int	desired_bif;		/* The "best implementation for" value we will look for. */
					/* See mult.asm for defined BIF_ values. */

/* If there are no implementations, return false.  This can happen for SSE2 and earlier implementations where there are no best-impl-for */
/* settings or only for an architecture (K8) we can't run.  Examples include SSE2 FFT lengths 2000K and 3456K in 32-bit mode. */

	if (! (gwdata->cpu_flags & CPU_3DNOW_PREFETCH)) {
		for ( ; jmptab->flags & 0x80000000; jmptab = INC_JMPTAB (jmptab)) {
			int	arch = (jmptab->flags >> 17) & 0xF;
			if (arch != ARCH_K8 && arch != ARCH_K10) break;
		}
	}
	if (!(jmptab->flags & 0x80000000)) return (FALSE);

/* Determine the "bif" value we will look for.  Often this is a straight-forward mapping from */
/* the CPU_ARCHITECTURE.  However, for some CPU architectures, like Pentium M and Core Solo, we */
/* don't have jmptable entries detailing the fastest FFT implementations for those architectures. */

	desired_bif = calculate_bif (gwdata, jmptab->fftlen, negacyclic);

/* Assume we will use the old school method (no benchmark database) where the */
/* best FFT implementation is hardwired into the mult.asm jmptable. */

	*best_impl_id = -1;

/* For small SSE2 FFTs as well as all x87 FFTs there is only one implementation -- use it. */

	if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
		if (gwdata->cpu_flags & CPU_SSE2) {
			if (jmptab->fftlen < 7168) return (TRUE);
		} else
			return (TRUE);
	}

/* If we are benchmarking all FFT implementations or we are doing QA, then we want to test */
/* this FFT length even if it isn't optimal */

	if (gwdata->bench_pick_nth_fft || gwdata->qa_pick_nth_fft) return (TRUE);

/* If we are using benchmark data to select fastest FFT implementation check the benchmark database to see */
/* if there is benchmark data available and that a larger FFT size will not be faster. */

	if (gwdata->use_benchmarks) {
		int	arch, num_cores, num_workers, num_hyperthreads, i;

/* Calculate the architecture, number of cores, workers, and hyperthreads for looking up the right benchmark data */

		switch (desired_bif) {			/* Only get benchmark data for relevant CPU architecture */
		case BIF_RYZEN:
			arch = (gwdata->cpu_flags & CPU_AVX512F) ? ARCH_SKX : ARCH_FMA3;
			break;
		case BIF_SKX:
			arch = ARCH_SKX;
			break;
		case BIF_FMA3:
			arch = ARCH_FMA3;
			break;
		case BIF_I7:
		case BIF_CORE2:
		case BIF_CORE2_512:
			arch = ARCH_CORE;
			break;
		case BIF_K10:
			arch = ARCH_K10;
			break;
		case BIF_K8:
			arch = ARCH_K8;
			break;
		default:
			arch = ARCH_P4;
		}
		if (gwdata->will_hyperthread) {
			num_hyperthreads = gwdata->will_hyperthread;		/* User can tell us more than 2 hyperthreads will be used */
			if (num_hyperthreads <= 1) num_hyperthreads = 2;	/* Minimum hyperthread count is two */
		} else
			num_hyperthreads = 1;
		num_cores = gwdata->bench_num_cores;				/* Use suggested value from caller */
		if (num_cores == 0) num_cores = CPU_CORES;			/* Else default to all cores will be busy */
		num_workers = gwdata->bench_num_workers;			/* Use suggested value from caller */
		if (num_workers == 0) num_workers = num_cores * num_hyperthreads / gwdata->num_threads; /* Else default worker count */

/* See if the benchmark database has bench data either with or without error checking. */
/* Once we have throughput data from the benchmark database, make sure a slightly larger */
/* FFT length will not offer even more throughput. */

		for (i = 0; i <= 1; i++) {
			const struct gwasm_jmptab *next_jmptab;
			int	error_check, impl, next_impl;
			double	throughput, next_throughput;

			if (gwdata->will_error_check == 0) error_check = i;	/* Look for no-error-checking benchmarks first */
			if (gwdata->will_error_check == 1) error_check = !i;	/* Look for error-checking benchmarks first */
			if (gwdata->will_error_check == 2) error_check = i;	/* Need a more sophisticated approach in this case */
			gwbench_get_max_throughput (jmptab->fftlen, arch, num_cores, num_workers, num_hyperthreads,
						    negacyclic, error_check, &impl, &throughput);
			if (throughput <= 0.0) continue;

			for (next_jmptab = NEXT_SET_OF_JMPTABS(jmptab); ; next_jmptab = NEXT_SET_OF_JMPTABS(next_jmptab)) {
				if (next_jmptab->fftlen == 0 ||				/* There is no next FFT length */
				    next_jmptab->fftlen > 1.03 * jmptab->fftlen) {	/* Next FFT length is much bigger (and therefore slower) */
					*best_impl_id = impl;
					return (TRUE);
				}
				gwbench_get_max_throughput (next_jmptab->fftlen, arch, num_cores, num_workers, num_hyperthreads,
							    negacyclic, error_check, &next_impl, &next_throughput);
				if (next_throughput <= 0.0) continue;			/* No bench data, assume larger FFT will be slower */
				if (next_throughput > throughput) return (FALSE);	/* Larger FFT length is faster */
			}
		}
	}

/* Loop through the FFT implementations to see if we find an implementation that matches our desired "bif" value. */

	while (jmptab->flags & 0x80000000) {
		if (((jmptab->flags >> 13) & 0xF) == desired_bif) return (TRUE);
		if (gwdata->required_pass2_size && ((jmptab->flags >> 13) & 0xF) == 0) return (TRUE);
		jmptab = INC_JMPTAB (jmptab);
	}

/* FFT implementation not found.  A larger FFT length should be faster. */

	return (FALSE);
}

/* Some gwinfo calculations depend on whether this is a one-pass or two-pass FFT. */
/* However, some FFTs have both a one-pass and two-pass implementation.  In such cases, */
/* we must make sure that jmptab points to the implementation that we will actually use. */

const struct gwasm_jmptab *choose_one_pass_or_two_pass_impl (
	gwhandle *gwdata,	/* Gwnum global data */
	const struct gwasm_jmptab *jmptab,
	int	negacyclic)
{
	const struct gwasm_jmptab *orig_jmptab;	
	int	desired_bif;		/* The "best implementation for" value we will look for. */
					/* See mult.asm for defined BIF_ values. */

/* For small SSE2 FFTs as well as all x87 FFTs, there is one implementation and it is always available */

	if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
		if (gwdata->cpu_flags & CPU_SSE2) {
			if (jmptab->fftlen < 7168) return (jmptab);
		} else
			return (jmptab);
	}

/* If we are benchmarking all FFT implementations, then we always want to start with the first implementation */

	if (gwdata->bench_pick_nth_fft) return (jmptab);

/* If the first entry is a two-pass implementation, then all the implementations are two-pass */
/* Use the first entry, as any of the entries will do for our purposes */

	if ((jmptab->flags & 0x1FF) != 0) return (jmptab);

/* Determine the "bif" value we will look for.  Often this is a straight-forward mapping from */
/* the CPU_ARCHITECTURE.  However, for some CPU architectures, like Pentium M and Core Solo, we */
/* don't have jmptable entries detailing the fastest FFT implementations for those architectures. */

	desired_bif = calculate_bif (gwdata, jmptab->fftlen, negacyclic);

/* Loop through the FFT implementations to see if we find a one-pass implementation */
/* that matches our desired "bif" value.  Otherwise, return first two-pass implementation. */

	orig_jmptab = jmptab;
	while (jmptab->flags & 0x80000000) {
		if ((jmptab->flags & 0x1FF) != 0) return (jmptab);
		if (((jmptab->flags >> 13) & 0xF) == desired_bif) return (orig_jmptab);
		jmptab = INC_JMPTAB (jmptab);
	}

/* FFT implementation not found.  Can't happen as is_fft_implemented should have been called. */

	return (orig_jmptab);
}

/* FMA3 FFTs compared to AVX FFTs have on average 7% less round off error.  This lets us have a higher max exponent. */
/* Increase max_exp by (1-SQRT(.93)) * FFTlen.  We'll be a little more conservative for smaller FFT lengths. */

unsigned long adjusted_max_exponent (
	const gwhandle *gwdata,			/* Gwnum global data */
	const struct gwasm_jmptab *jmptab)	/* Jmptable entry */
{
	unsigned long max_exp;

	max_exp = jmptab->max_exp;
	if (max_exp == 0) return (0);
	if (!(gwdata->cpu_flags & CPU_AVX512F) && gwdata->cpu_flags & CPU_FMA3) {
		if (jmptab->fftlen >= 1048576) max_exp += (int) (0.035635 * (double) jmptab->fftlen);
		else if (jmptab->fftlen >= 32768) max_exp += (int) (0.017656 * (double) jmptab->fftlen);
	}
	return (max_exp);
}

/* This routine used to be in assembly language.  It scans the assembly */
/* code arrays looking for the best FFT size to implement our k*b^n+c FFT. */
/* Returns 0 for IBDWT FFTs, 1 for zero padded FFTs, or a gwsetup error code. */

int gwinfo (			/* Return zero-padded fft flag or error code */
	gwhandle *gwdata,	/* Gwnum global data */
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* N in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	struct gwinfo1_data asm_info;
	const struct gwasm_jmptab *jmptab, *zpad_jmptab, *generic_jmptab, *impl_jmptab;
	int	best_impl_id, zpad_best_impl_id;
	double	log2k, logbk, log2b, log2c, log2maxmulbyconst;
	double	max_bits_per_input_word, max_bits_per_output_word;
	double	max_weighted_bits_per_output_word;
	int	num_b_in_big_word, num_small_words, num_big_words;
	double	b_per_input_word, bits_per_output_word;
	double	weighted_bits_per_output_word;
	unsigned long max_exp;
	char	buf[20];
	int	larger_fftlen_count, qa_nth_fft, desired_bif;
	void	*prev_proc_ptrs[5];
	uint32_t flags;
	float	safety_margin;
	struct gwasm_data *asm_data;

/* Get pointer to 8 assembly jmptables and the version number */

	gwinfo1 (&asm_info);

/* Make sure that the assembly code version number matches the C version */
/* number.  If they do not match, then the user linked in the wrong gwnum */
/* object files! */

	sprintf (buf, "%d.%d", asm_info.version / 100, asm_info.version % 100);
	if (strcmp (buf, GWNUM_VERSION)) return (GWERROR_VERSION);

/* Precalculate some needed values */

	log2k = log2 (k);
	log2b = log2 (b);
	logbk = log2k / log2b;
	log2c = log2 (labs (c));
	log2maxmulbyconst = log2 (gwdata->maxmulbyconst);
	safety_margin = gwdata->safety_margin + gwdata->polymult_safety_margin;

/* The smallest AVX-512F FFT is length 1K.  Limited testing has indicated that less than 5 bits per FFT word */
/* can result in carry propagation errors.  For small k*b^n+c values switch to not using AVX-512 instructions. */
/* Don't catch the n==0 case from gwmap_with_cpu_flags_fftlen_to_max_exponent. */

	if (n && log2b * (double) n < 5.0 * 1024.0)
		gwdata->cpu_flags &= ~CPU_AVX512F;

/* First, see what FFT length we would get if we emulate the k*b^n+c modulo */
/* with a zero padded FFT.  If k is 1 and abs (c) is 1 then we can skip this */
/* loop as we're sure to find an IBDWT that will do the job. Also skip if called from */
/* gwmap_fftlen_to_max_exponent (n = 0) or we are QAing IBDWT FFTs (qa_pick_nth_fft >= 1000) */

again:	zpad_jmptab = NULL;
	generic_jmptab = NULL;
	if (! gwdata->force_general_mod && (k > 1.0 || (n > 0 && n < 500) || labs (c) > 1) && gwdata->qa_pick_nth_fft < 1000) {
		larger_fftlen_count = gwdata->larger_fftlen_count;

/* Use the proper 2^N-1 jmptable */

		if (gwdata->cpu_flags & CPU_AVX512F)
			zpad_jmptab = asm_info.avx512_cyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_AVX)
			zpad_jmptab = asm_info.avx_cyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_SSE2)
			zpad_jmptab = asm_info.sse2_cyclic_fft_info;
		else
			zpad_jmptab = asm_info.x86_cyclic_fft_info;

/* If no jmptable was found (should only happen if caller has disabled all but the x87 CPU_FLAGS in 64-bit mode */

		if (zpad_jmptab == NULL) return (GWERROR_TOO_LARGE);

/* Find the table entry for the FFT that can do a mod 2^2n FFT, handling */
/* k and c in the normalization routines.  We will compare this to the */
/* non-zero-padded FFT length later.  The zeroes in the upper half of FFT */
/* input data let us get about another 0.3 bits per input word. */

		while ((max_exp = adjusted_max_exponent (gwdata, zpad_jmptab)) != 0) {

/* Do a quick check on the suitability of this FFT */

			if ((double) (n + n) * log2b / (double) zpad_jmptab->fftlen > 27.0 - 0.25 * log2 (zpad_jmptab->fftlen)) goto next1;
			if (zpad_jmptab->fftlen < gwdata->minimum_fftlen) goto next1;

/* Don't bother looking at this FFT length if the generic reduction would be faster */

			if (generic_jmptab != NULL && zpad_jmptab->timing > 3.0 * generic_jmptab->timing) goto next1;

/* Make sure this FFT length is implemented and benchmarking does not show that a larger FFT will be faster */

			if (! is_fft_implemented (gwdata, FALSE, zpad_jmptab, &zpad_best_impl_id)) goto next1;

/* See if this is the FFT length that would be used for a generic modulo reduction */

			if (generic_jmptab == NULL && 2.0 * (log2k + n * log2b) + 128.0 < max_exp + 0.15 * zpad_jmptab->fftlen)
				generic_jmptab = zpad_jmptab;

/* Compare the maximum number of bits allowed in the FFT input word with the number of bits we would use.  Break when we find an acceptable FFT length. */
//  This is the old code which only supported b == 2
//			max_bits_per_word = (double) max_exp / zpad_jmptab->fftlen;
//			max_bits_per_word -= safety_margin;
//			bits_per_word = (double) (n + n) * log2b / zpad_jmptab->fftlen;
//			if (bits_per_word < max_bits_per_word + 0.3) {
//				break;
//			}

/* In version 25.11, we need to handle b != 2.  See comments later on in this routine */
/* for a description of the concepts involved. */

/* Compute the maximum number of bits allowed in the FFT input word */

			max_bits_per_input_word = (double) max_exp / zpad_jmptab->fftlen;
			max_bits_per_input_word -= safety_margin;

/* Apply our new formula (described later) to the maximum Mersenne exponent for this FFT length. */

			num_b_in_big_word = (int) ceil (max_bits_per_input_word);
			num_small_words = (int) ((num_b_in_big_word - max_bits_per_input_word) * zpad_jmptab->fftlen);
			num_big_words = zpad_jmptab->fftlen - num_small_words;
			max_bits_per_output_word =
				2 * (num_b_in_big_word - 1) +
				0.6 * log2 (num_big_words + num_small_words / 3.174802103936252);

/* Apply our new formula (described later) to the number we are planning to test.  */
/* This is different for the zero-pad case because only 4 words in the upper half */
/* of the FFT contain any data.  We can't use the FFT length if the k value will */
/* not fit in 4 words. */

			b_per_input_word = (double) (n + n) / zpad_jmptab->fftlen;
			if (logbk > 4.0 * b_per_input_word) goto next1;
			num_b_in_big_word = (int) ceil (b_per_input_word);
			num_small_words = (int) ((num_b_in_big_word - b_per_input_word) * (zpad_jmptab->fftlen / 2 + 4));
			num_big_words = (zpad_jmptab->fftlen / 2 + 4) - num_small_words;
			bits_per_output_word =
				2.0 * (num_b_in_big_word * log2b - 1.0) +
				0.6 * log2 (num_big_words + num_small_words / pow (2.0, log2b / 0.6));

/* And compute the weighted values as per the formulas described later.  In 29.7 we added the log2b < 12.5 case (to match */
/* the non-zero-padded code) as 27904^53415-7 was choosing a zero-padded 200K AVX-512 FFT and getting roundoffs frequently */
/* above 0.4 and rarely above 0.5.  We need to do a careful analysis using average round error to fine tune this adjustment further. */

			max_weighted_bits_per_output_word = 2.0 * max_bits_per_input_word + 0.6 * log2 (zpad_jmptab->fftlen / 2 + 4);
			weighted_bits_per_output_word = 2.0 * ((b_per_input_word + 1.0) * log2b - 1.0) + 0.6 * log2 (zpad_jmptab->fftlen / 2 + 4);
			if ((n + n) % zpad_jmptab->fftlen == 0)
				weighted_bits_per_output_word -= ((log2b <= 4.0) ? log2b : 1.4 * log2b);
			else if (! is_pathological_distribution (num_big_words, num_small_words))
				weighted_bits_per_output_word -=
					((log2b <= 3.0) ? (log2b - 1.0) / 2.0 :
					 (log2b <= 6.0) ? 1.0 + (log2b - 3.0) / 3.0 :
					 (log2b <= 12.5) ? 2.0 + (log2b - 6.0) / 6.5 : 3.0);

/* See if this FFT length might work */

			if ((weighted_bits_per_output_word <= max_weighted_bits_per_output_word) &&

/* Result words are multiplied by k and the mul-by-const and any carry spread over 6 words. */
/* Thus, the multiplied FFT result word cannot be more than 7 times bits-per-input-word */
/* (bits-per-input-word are stored in the current word and the 6 words we propagate carries to). */

			    bits_per_output_word + log2k + log2maxmulbyconst <= floor (7.0 * b_per_input_word) * log2b &&

/* The high part of upper result words are multiplied by c and the mul-by-const.  This must not exceed 51 bits. */

			    bits_per_output_word - floor (b_per_input_word) * log2b + log2c + log2maxmulbyconst <= 51.0) {

/* This FFT could work, but see if the user requested a larger than normal FFT size */

				if (larger_fftlen_count--) goto next1;

/* We can use this FFT.  Look for a non-zero-padded FFT that might be even faster. */

				break;
			}

/* Move past procedure entries and counts to next jmptable entry */

next1:			zpad_jmptab = NEXT_SET_OF_JMPTABS (zpad_jmptab);
		}

/* Some k/b/n/c values can't be handled by AVX-512 FFTs because there are relatively few small FFT sizes available. */
/* In these cases, we'll retry using FMA3 FFTs.  One example is 15312853462553*2^5257+1.  NOTE:  There may be non-base2 cases */
/* where we could use a zero-padded FMA3 FFT, but instead we'll end up selecting a generic reduction AVX-512 FFT. */

		if (zpad_jmptab->max_exp == 0 && gwdata->cpu_flags & CPU_AVX512F && b == 2 && n < 100000) {
			gwdata->cpu_flags &= ~CPU_AVX512F;
			goto again;
		}
	}

/* Now see what FFT length we would use if a DWT does the k*b^n+c modulo. */

/* Use the proper 2^N+1 or 2^N-1 jmptable */

	if (c < 0) {
		if (gwdata->cpu_flags & CPU_AVX512F)
			jmptab = asm_info.avx512_cyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_AVX)
			jmptab = asm_info.avx_cyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_SSE2)
			jmptab = asm_info.sse2_cyclic_fft_info;
		else
			jmptab = asm_info.x86_cyclic_fft_info;
	} else {
		if (gwdata->cpu_flags & CPU_AVX512F)
			jmptab = asm_info.avx512_negacyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_AVX)
			jmptab = asm_info.avx_negacyclic_fft_info;
		else if (gwdata->cpu_flags & CPU_SSE2)
			jmptab = asm_info.sse2_negacyclic_fft_info;
		else
			jmptab = asm_info.x86_negacyclic_fft_info;
	}

/* If no jmptable was found (should only happen if caller has disabled all but the x87 CPU_FLAGS in 64-bit mode */

	if (jmptab == NULL) return (GWERROR_TOO_LARGE);

/* Find the table entry using either the specified fft length or */
/* the smallest FFT length that can handle the k,b,n,c being tested. */

	larger_fftlen_count = gwdata->larger_fftlen_count;
	while ((max_exp = adjusted_max_exponent (gwdata, jmptab)) != 0) {

/* Do a quick check on the suitability of this FFT */

		if ((double) n * log2b / (double) jmptab->fftlen > 26.0 - 0.25 * log2 (jmptab->fftlen)) goto next2;
		if (jmptab->fftlen < gwdata->minimum_fftlen) goto next2;

/* Top carry adjust can only handle k values of 34 bits or less */

		if (log2k >= 34.0) goto next2;

/* Make sure this FFT length is implemented and benchmarking does not show that a larger FFT will be faster */

		if (! is_fft_implemented (gwdata, c > 0, jmptab, &best_impl_id)) goto next2;

/* Always use the minimum_fftlen if n is zero (a special case call from gwmap_fftlen_to_max_exponent) */

		if (gwdata->minimum_fftlen && n == 0) break;

/* Some calculations below depend on whether this is a one-pass or two-pass FFT. */
/* However, some FFTs have both a one-pass and two-pass implementation.  In such cases, */
/* we must make sure that jmptab points to the implementation that we will actually use. */

		impl_jmptab = choose_one_pass_or_two_pass_impl (gwdata, jmptab, c > 0);

/* Check if this FFT length will work with this k,n,c combo */

//  This is the old code which only supported b == 2
//		double max_bits_per_word;
//		double bits_per_word;
//
/* Compute the maximum number of bits allowed in the FFT input word */
//
//		max_bits_per_word = (double) max_exp / jmptab->fftlen;
//		max_bits_per_word -= safety_margin;
//
/* For historical reasons, the jmptable computes maximum exponent based on */
/* a Mersenne-mod FFT (i.e k=1.0, c=-1).  Handle more complex cases here. */
/* A Mersenne-mod FFT produces 2 * bits_per_word in each FFT result word. */
/* The more general case yields 2 * bits_per_word + log2(k) + 1.7 * log2(c) */
/* in each FFT result word.  NOTE: From the data I've observed, doubling c */
/* about triples the roundoff error (that is log2(3) = 1.585 output bits). */
/* However, when I used 1.585 in the formula it was not hard to find cases */
/* where the roundoff error was too high, so we use 1.7 here for extra */
/* safety. */
//
//		bits_per_word = (log2k + n * log2b) / jmptab->fftlen;
//		if (2.0 * bits_per_word + log2k + 1.7 * log2c <=
//					2.0 * max_bits_per_word) {
/* Because carries are spread over 4 words, there is a minimum limit on */
/* the bits per word.  An FFT result word cannot be more than 5 times */
/* bits-per-word (bits-per-word are stored in the current word and the */
/* 4 words we propagate carries to).  How many bits are in an FFT result */
/* word?  Well, because of balanced representation the abs(input word) is */
/* (bits_per_word-1) bits long. An FFT result word contains multiplied data */
/* words, that's (bits_per_word-1)*2 bits.  Adding up many multiplied data */
/* words adds some bits proportional to the size of the FFT.  Experience */
/* has shown this to be 0.6 * log (FFTLEN).  This entire result is */
/* multiplied by k in the normalization code, so add another log2(k) bits. */
/* Finally, the mulbyconst does not affect our chance of getting a round off */
/* error, but does add to the size of the carry. */
//
//		loglen = log2 (jmptab->fftlen);
//		total_bits = (bits_per_word - 1.0) * 2.0 +
//				     1.7 * log2c + loglen * 0.6 +
//				     log2k + log2maxmulbyconst;
//		if (total_bits > 5.0 * bits_per_word) {

/* In version 25.11, we now need to handle b != 2.  Consider the case */
/* where b is ~4000.  If small words contain one b (~12 bits) and large words */
/* contain two b (~24 bits), then the number of bits in a result word is */
/* dominated by big words * big word products (~48 bits).  The old code above */
/* tested average bits per word (~18 bits) and underestimates a result word as */
/* containing ~36 bits.  So here's our upgraded model.  We calculate the number */
/* of big and little words.  A result word adds up many big word times big word */
/* products and big word times small word products.  Let base = b ^ num_b_in_big_word. */
/* Because of balanced representation, a big word times big word */
/* product is 2 * (log2(base) - 1) bits.  Summing them up adds about */
/* 0.6 * log2 (num_big_words) more bits.  Now for the big words times */
/* small words products that are also added in, the more bits in a small word the more */
/* it impacts the result word.  A big word times small word product has log2(b) fewer */
/* bits in it.  If we add two big word times small word products, the sum is */
/* about 0.6 bits bigger, add four products to get 1.2 bits bigger, etc. -- do this until you */
/* overcome the log2(b) bit difference.  That is, 2^(log2(b)/0.6) small */
/* products equals one big product.  Putting it all together, a result word contains */
/* 2 * (log2(base) - 1) + 0.6 * log2 (num_big_words + num_small_words / 2^(log2(b)/0.6)) */
/* bits plus the k and c adjustments noted above. */

/* Compute the maximum number of bits allowed in the FFT input word */

		max_bits_per_input_word = (double) max_exp / jmptab->fftlen;
		max_bits_per_input_word -= safety_margin;

/* Apply our new formula above to the maximum Mersenne exponent for this FFT length. */

		num_b_in_big_word = (int) ceil (max_bits_per_input_word);
		num_small_words = (int) ((num_b_in_big_word - max_bits_per_input_word) * jmptab->fftlen);
		num_big_words = jmptab->fftlen - num_small_words;
		max_bits_per_output_word =
				2 * (num_b_in_big_word - 1) +
				0.6 * log2 (num_big_words + num_small_words / 3.174802103936252);

/* Apply our new formula to the number we are planning to test.  In version 26.3 we changed */
/* "2.0 * (num_b_in_big_word * log2b - 1.0)" to "floor (2.0 * b_per_input_word) * log2b - 2.0" */
/* because of examples like 10024*603^153-1 which has num_b_in_big_word = 1 and b_per_input_word = 0.8. */
/* This means weighted values are as large as b^1.8.  Squaring that large value yields b^3.6.  Apply */
/* the inverse weight of b^-.6 and we have output values as large as b^3.  The improved formula */
/* reflects these larger output values. */

		b_per_input_word = (logbk + n) / jmptab->fftlen;
		num_b_in_big_word = (int) ceil (b_per_input_word);
		num_small_words = (int) ((num_b_in_big_word - b_per_input_word) * jmptab->fftlen);
		num_big_words = jmptab->fftlen - num_small_words;
		if (gwdata->radix_bigwords && num_big_words > gwdata->radix_bigwords) num_big_words = gwdata->radix_bigwords;
		if (k == 1.0 && n % jmptab->fftlen == 0)
			bits_per_output_word = 
				2.0 * (num_b_in_big_word * log2b - 1.0) +
				0.6 * log2 (num_big_words + num_small_words / pow (2.0, log2b / 0.6)) +
				log2k + 1.7 * log2c;
		else
			bits_per_output_word = 
				floor (2.0 * (b_per_input_word + 1.0)) * log2b - 2.0 +
				0.6 * log2 (num_big_words + num_small_words / pow (2.0, log2b / 0.6)) +
				log2k + 1.7 * log2c;

/* Unfortunately, the story does not end there.  The weights applied to each FFT word */
/* range from 1 to b.  These extra bits impact the round off error.  Thus, we calculate */
/* the weighted_bits_per_output_word for irrational FFTs as using another log2b bits. */

		max_weighted_bits_per_output_word = 2.0 * max_bits_per_input_word + 0.6 * log2 (jmptab->fftlen);
		if (k == 1.0 && n % jmptab->fftlen == 0) {
			/* New in 29.7: 1889^20480+1 was generating too large a roundoff using a 10K AVX-512 FFT. */
			/* Testing shows that an irrational FFT has about twice the average roundoff error as a */
			/* rational FFT (equals one bit in the output word).  But comparing 2^204800+1 and 2^204801+1 */
			/* in a 10K FFT, our code calculates a two bit difference between weighted_bits_per_output_word */
			/* and max_weighted_bits_per_output_word.  The proper solution is to correct the calculation of */
			/* max_weighted_bits_per_output_word and irrational weighted_bits_per_output_word.  However, */
			/* I'm a little leery of changing that working code so I'm just adding one to the rational case here. */
			weighted_bits_per_output_word =	bits_per_output_word + 1.0;
		} else {
			weighted_bits_per_output_word =
				2.0 * ((b_per_input_word + 1.0) * log2b - 1.0) +
				0.6 * log2 (jmptab->fftlen) + log2k + 1.7 * log2c;

/* A pathological case occurs when num_big_words is one and k is greater than one. */
/* The FFT weights for the small words will not range from 1 to b.  Depending on the */
/* fractional part of logb(k).  In the worst case scenario, the small word weights */
/* range from b - epsilon to b.  The example that raised this issue is 28*3^12285-1. */

			if (num_big_words == 1 && k > 1.0)
				weighted_bits_per_output_word += log2b;

/* Furthermore, testing shows us that larger b values don't quite need the full log2b */
/* bits added (except for some pathological cases), probably because there are fewer */
/* extra bits generated by adding products because the smallest weighted words have */
/* fewer bits.  The correction is if log2b is 3 you can get 1 more output bit than */
/* expected, if log2b is 6 you get about 2 extra bits, if log2b is 12 you can get */
/* 3 extra bits.  This formula was found to be a bit too aggressive, at least for */
/* large b.  Two examples:  2*22563^22563-1 and  2*22576^22576-1 fail in a 40K FFT. */
/* Thus, we're changing the formula in v27.9 to log2b of 12.5 gets the max of 3 extra bits. */
/* Also, some examples such as 19464*19^31895+1 and 245*830^492-1 (worst case we */
/* know of) still raise round off errors.  For added safety we assume an extra */
/* 0.3 bits of output are needed when base is not 2. */

			else if (! is_pathological_distribution (num_big_words, num_small_words)) {
				weighted_bits_per_output_word -=
					((log2b <= 3.0) ? (log2b - 1.0) / 2.0 :
					 (log2b <= 6.0) ? 1.0 + (log2b - 3.0) / 3.0 :
					 (log2b <= 12.5) ? 2.0 + (log2b - 6.0) / 6.5 : 3.0);
				if (b != 2) weighted_bits_per_output_word += 0.3;
			}
		}

/* If the bits in an output word is less than the maximum allowed then we can probably use this FFT length -- though we need to do a few more tests. */

		if (weighted_bits_per_output_word <= max_weighted_bits_per_output_word) {
			double	carries_spread_over;

/* Originally, carries were spread over 4 FFT words.  Some FFT code has been */
/* upgraded to spread the carry over 6 FFT words.  Handle that here.  Note that */
/* FFT lengths 80 and 112 were not upgraded. */

			if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2)))
				carries_spread_over = 4.0;
			else if (jmptab->fftlen == 80 || jmptab->fftlen == 112)
				carries_spread_over = 4.0;
			else if ((impl_jmptab->flags & 0x1FF) == 0)	// One pass AVX-512/AVX/SSE2 FFTs
				carries_spread_over = 6.0;
			else						// Two pass AVX-512/AVX/SSE2 FFTs
				carries_spread_over = 6.0;

/* Because carries are spread over 4 words, there is a minimum value for the bits per FFT word.  An FFT result word must fit in the */
/* floor(bits-per-input-word) stored in the current word plus ceil (4 * bits-per-input-word) for the carries to  propagate into. */
/* The mul-by-const during the normalization process adds to the size of the result word. */

			if (!gwdata->radix_bigwords &&			// Radix conversion code has no trouble spreading carries (we think)
			    bits_per_output_word + log2maxmulbyconst > (floor (b_per_input_word) + ceil (carries_spread_over * b_per_input_word)) * log2b) {
// This assert was designed to find any cases where using more carry words
// would use a shorter FFT than using a zero-padded FFT.  There are many such
// cases, especially with larger bases.  One example is 5001*500^100000-1.
// It would use 132K FFT length if I implemented 7 carry words
// and requires a 144K FFT length for a zero-padded FFT.
//				ASSERTG (zpad_jmptab == NULL || jmptab->fftlen >= zpad_jmptab->fftlen);
				goto next2;
			}

/* Because of limitations in the top_carry_adjust code, there is a limit */
/* on the size of k that can be handled.  This isn't a big deal since the */
/* zero-padded implementation should use the same FFT length.  Check to see */
/* if this k can be handled.  K must fit in the top three words for */
/* one-pass FFTs and within the top two words of two-pass FFTs. */

			if ((impl_jmptab->flags & 0x1FF) == 0 &&
			    logbk > ceil (jmptab->fftlen * b_per_input_word) - ceil ((jmptab->fftlen-3) * b_per_input_word)) {
// This assert is designed to find any cases where using 4 or more carry adjust words
// would use a shorter FFT than using a zero-padded FFT.  We found an example:
// 102233*299^239-1.  It would use length 256 FTT versus a length 320 zero-padded FFT.
//				ASSERTG (zpad_jmptab == NULL || jmptab->fftlen >= zpad_jmptab->fftlen);
				goto next2;
			}

			if ((impl_jmptab->flags & 0x1FF) != 0 &&
			    logbk > ceil (jmptab->fftlen * b_per_input_word) - ceil ((jmptab->fftlen-2) * b_per_input_word)) {
// This assert is designed to find any cases where using 3 or more carry adjust words
// would use a shorter FFT than using a zero-padded FFT.  One example: 501*500^100000-1
// It would use a 112K FFT instead of a 144K zero-padded FFT.
//				ASSERTG (zpad_jmptab == NULL || jmptab->fftlen >= zpad_jmptab->fftlen);
				goto next2;
			}

/* This FFT could work, but see if the user requested a larger than normal FFT size */

			if (larger_fftlen_count--) goto next2;

/* We've found an FFT length to use */

			break;
		}

/* Move past procedure entries and counts to next jmptable entry */

next2:		jmptab = NEXT_SET_OF_JMPTABS (jmptab);
	}

/* If the zero pad FFT length is less than the DWT FFT length OR we */
/* are QA'ing every FFT implementation, then use the zero pad FFT length. */

	if (zpad_jmptab != NULL && zpad_jmptab->max_exp &&
	    (jmptab->max_exp == 0 || zpad_jmptab->fftlen < jmptab->fftlen || gwdata->qa_pick_nth_fft)) {
		gwdata->ZERO_PADDED_FFT = TRUE;
		gwdata->NEGACYCLIC_FFT = FALSE;
		jmptab = zpad_jmptab;
		best_impl_id = zpad_best_impl_id;
	}

/* If we found a DWT table entry then use it. */

	else if (jmptab->max_exp) {
		gwdata->ZERO_PADDED_FFT = FALSE;
		gwdata->NEGACYCLIC_FFT = (c > 0);
	}

/* Error - neither method could handle this huge number */

	else
		return (GWERROR_TOO_LARGE);

/* We've found the right "jump" table entry, save the pointer and FFT length */

	gwdata->jmptab = jmptab;
	gwdata->FFTLEN = jmptab->fftlen;

/************************************************************************/
/* Decide which implementation of this FFT length is best for this CPU. */
/************************************************************************/

/* Loop through all the implementations for this FFT length until we find the one best suited to this CPU. */

	qa_nth_fft = gwdata->ZERO_PADDED_FFT ? 100 : 1000;
	desired_bif = calculate_bif (gwdata, gwdata->FFTLEN, gwdata->NEGACYCLIC_FFT);
	prev_proc_ptrs[0] = NULL;
	prev_proc_ptrs[1] = NULL;
	prev_proc_ptrs[2] = NULL;
	prev_proc_ptrs[3] = NULL;
	prev_proc_ptrs[4] = NULL;
	for ( ; ; ) {
		int	arch;		/* (blend=0,p3=1,p4=2,core=3,fma3=4,k8=5,etc.) */
		int	best_impl_for;	/* (CORE2=0,I7=1,etc.  See BIF_ definitions in mult.asm) */
		int	fft_type;	/* (home-grown=0, radix-4=1, r4delay=2, r4dwpn=3) */

/* Handle an FFT implementation not found condition.  Should only happen */
/* if we're benchmarking or QA'ing and we've tested every implementation. */

		if (! (jmptab->flags & 0x80000000)) {

/* If we are QA'ing every FFT implementation and we did not find another */
/* zero-padded FFT implementation, then go find a non-zero-padded one. */

			if (gwdata->qa_pick_nth_fft && gwdata->qa_pick_nth_fft < 1000) {
				gwdata->qa_pick_nth_fft = 1000;
				goto again;
			}

/* Else return an error */

			return (GWERROR_TOO_LARGE);
		}

/* If this CPU will crash running this FFT then skip this entry. */
/* Our K8 and K10 optimized FFTs requires prefetchw (3DNow!) capability. */
/* If this is an Intel CPU, skip over these implementations. */

		arch = (jmptab->flags >> 17) & 0xF;
		if ((arch == ARCH_K8 || arch == ARCH_K10) && ! (gwdata->cpu_flags & CPU_3DNOW_PREFETCH))
			goto next3;

/* If this CPU will crash running this FFT then skip this entry. */
/* Our FMA3 FFTs require support of the Intel FMA3 instruction. */

		if (arch == ARCH_FMA3 && ! (gwdata->cpu_flags & CPU_FMA3))
			goto next3;

/* Handle benchmarking case that selects the nth FFT implementation */
/* without regard to any other consideration.  NOTE: Due to the extreme */
/* penalty a K8 pays for using the movaps instruction that the Core and P4 */
/* implementations use, we will not benchmark these on a K8.  Also, since */
/* FMA3 FFTs always outperform their non-FMA3 alternatives, we'll skip the */
/* non-FMA3 FFTs. */

		if (gwdata->bench_pick_nth_fft) {
			if (jmptab->proc_ptr == prev_proc_ptrs[0]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[1]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[2]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[3]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[4]) goto next3;
			if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K8 &&
			    (jmptab->flags & 0x1FF) != 0 &&
			    (arch == ARCH_P4 || arch == ARCH_P4TP || arch == ARCH_CORE))
				goto next3;
			if (gwdata->cpu_flags & CPU_FMA3 && (arch == ARCH_P4 || arch == ARCH_P4TP || arch == ARCH_CORE))
				goto next3;
			gwdata->bench_pick_nth_fft--;
			if (gwdata->bench_pick_nth_fft) goto next3;
			break;
		}

/* Handle the QA case that tests every possible FFT implementation */
/* Remember the FFT returned so that we can return a different FFT to */
/* the QA code next time. */

		if (gwdata->qa_pick_nth_fft) {
			if (jmptab->proc_ptr == prev_proc_ptrs[0]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[1]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[2]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[3]) goto next3;
			if (jmptab->proc_ptr == prev_proc_ptrs[4]) goto next3;
			if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K8 &&
			    (arch == ARCH_P4 || arch == ARCH_P4TP || arch == ARCH_CORE))
				goto next3;
			qa_nth_fft++;
			if (qa_nth_fft <= gwdata->qa_pick_nth_fft) goto next3;
			gwdata->qa_picked_nth_fft = qa_nth_fft;
			break;
		}

/* If this is a small SSE2 FFT or an x87 FFT, then there is only one implementation */

		if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
			if (gwdata->cpu_flags & CPU_SSE2) {
				if (gwdata->FFTLEN < 7168) break;
			} else
				break;
		}

/* If Montgomery reduction requires us to pick a specific pass2 size then handle that here.  Also match ARCH so that we don't pair AVX with FMA3 FFTs. */

		if (gwdata->required_pass2_size) {
			int	flags, p2size, arch;
			flags = jmptab->flags;
			if ((flags & 0x0000001FF) == 511)
				p2size = 48;
			else if ((flags & 0x0000001FF) == 510)
				p2size = 80;
			else if ((flags & 0x0000001FF) == 509)
				p2size = 32768;
			else
				p2size = (flags & 0x0000001FF) << 6;
			arch = (flags >> 17) & 0x0000000F;
			if (p2size + arch == gwdata->required_pass2_size) break;
			goto next3;
		}

/* If we got a best_impl_id using the benchmark_database, then see if this jmptable matches the best_impl_id */
/* Unfortunately, this duplicates much of the flags parsing found later on in this routine. */

		fft_type = (jmptab->flags >> 21) & 0xF;
		if (best_impl_id != -1) {
			int	flags, fft_type, arch, clm, p2size, no_prefetch, in_place;
			flags = jmptab->flags;
			no_prefetch = (flags >> 27) & 0x00000001;
			in_place = (flags >> 26) & 0x00000001;
			fft_type = (flags >> 21) & 0x0000000F;
			arch = (flags >> 17) & 0x0000000F;
			clm = (flags >> 9) & 0x0000000F;
			if ((flags & 0x0000001FF) == 511)
				p2size = 48;
			else if ((flags & 0x0000001FF) == 510)
				p2size = 80;
			else if ((flags & 0x0000001FF) == 509)
				p2size = 32768;
			else
				p2size = (flags & 0x0000001FF) << 6;
			if (internal_implementation_ids_match (best_impl_id, gwdata->FFTLEN, fft_type, no_prefetch, in_place, p2size, arch, clm)) break;
		}

/* Otherwise use old school method where best FFT implementation is hardwired into mult.asm's jmptable. */
/* See if this is the best implementation for this CPU architecture */

		else {
			best_impl_for = (jmptab->flags >> 13) & 0xF;
			if (best_impl_for == desired_bif) break;
		}

/* Move onto the next FFT implementation */

next3:		prev_proc_ptrs[4] = prev_proc_ptrs[3];
		prev_proc_ptrs[3] = prev_proc_ptrs[2];
		prev_proc_ptrs[2] = prev_proc_ptrs[1];
		prev_proc_ptrs[1] = prev_proc_ptrs[0];
		prev_proc_ptrs[0] = jmptab->proc_ptr;
		jmptab = INC_JMPTAB (jmptab);
	}

/* Remember the information from the chosen FFT implementation */

	flags = jmptab->flags;
	gwdata->GWPROCPTRS[0] = jmptab->proc_ptr;
	if (jmptab->flags & 0x40000000) {
		struct gwasm_alt_jmptab *altjmptab = (struct gwasm_alt_jmptab *) jmptab;
		gwdata->mem_needed = altjmptab->mem_needed;
	} else
		gwdata->mem_needed = jmptab->mem_needed;

/* Break the flags word from the jmptable entry into its constituent parts */
/* The 32-bit flags word is as follows (copied from mult.asm): */
/*	80000000h		always on */
/*	40000000h		on if there are 2 proc ptrs (main FFT entry point, address of routine to do second FFT pass) */
/*	2 SHL 26		(no prefetching - not used by gwnum) */
/*	1 SHL 26		(in_place) */
/*	fft_type SHL 21		(hg=0, r4=1, r4delay=2) */
/*	arch SHL 17		(blend=0,p3=1,p4=2,core=3,fma3=4,k8=5,etc.) */
/*	best_impl_for SHL 13	(CORE2=0,I7=1,P4_1024=2,etc.) */
/*	clm SHL 9		(1,2,4,8) */
/*	pass2size_over_64	(many valid values) */

	gwdata->NO_PREFETCH_FFT = (flags >> 27) & 0x00000001;
	gwdata->IN_PLACE_FFT = (flags >> 26) & 0x00000001;
	gwdata->FFT_TYPE = (flags >> 21) & 0x0000000F;
	gwdata->ARCH = (flags >> 17) & 0x0000000F;
	if (gwdata->cpu_flags & CPU_AVX512F) gwdata->PASS1_CACHE_LINES = ((flags >> 9) & 0x0000000F) * 8;
	else if (gwdata->cpu_flags & CPU_AVX) gwdata->PASS1_CACHE_LINES = ((flags >> 9) & 0x0000000F) * 4;
	else gwdata->PASS1_CACHE_LINES = ((flags >> 9) & 0x0000000F) * 2;
	if ((flags & 0x0000001FF) == 511) gwdata->PASS2_SIZE = 48;
	else if ((flags & 0x0000001FF) == 510) gwdata->PASS2_SIZE = 80;
	else if ((flags & 0x0000001FF) == 509) gwdata->PASS2_SIZE = 32768;
	else gwdata->PASS2_SIZE = (flags & 0x0000001FF) << 6;
	if (gwdata->PASS2_SIZE) gwdata->PASS1_SIZE = gwdata->FFTLEN / gwdata->PASS2_SIZE; /* Real values in a pass1 section */
	else gwdata->PASS1_SIZE = 0;				/* One-pass FFT */
	if (gwdata->PASS1_SIZE == 2) gwdata->PASS1_SIZE = 0;	/* Don't treat AVX-512 one-pass wrapper as a true pass 1 */

/* Calculate the scratch area size -- needed by gwmemused without calling gwsetup */

	if (gwdata->PASS1_SIZE && !gwdata->IN_PLACE_FFT) {
		if (gwdata->cpu_flags & CPU_AVX512F) {		// AVX-512 scratch area size
			int	pass1_size, num_clmblks, clmblkdst, gaps;
			// Mimic the clmblkdst calculations in zmult.mac.  
			pass1_size = gwdata->PASS1_SIZE;
			num_clmblks = pass1_size >> 4;
			clmblkdst = gwdata->PASS1_CACHE_LINES * 128;
			if (pass1_size == 192 || pass1_size == 640 || pass1_size == 768 || pass1_size == 896 || pass1_size == 960 ||
			    pass1_size == 1152 || pass1_size == 1280 || pass1_size == 1344 || pass1_size == 1536 || pass1_size == 1920 ||
			    pass1_size == 2304) { // Oddball pass 1 sizes
				gwdata->SCRATCH_SIZE = num_clmblks * (clmblkdst + 64) - 64;
			} else if (gwdata->PASS1_CACHE_LINES == 8) {	// clm=1
				gaps = num_clmblks / 8 - 1;
				gwdata->SCRATCH_SIZE = num_clmblks * clmblkdst + gaps * 128;
			} else if (gwdata->PASS1_CACHE_LINES == 16) {	// clm=2
				gaps = num_clmblks / 8 - 1;
				gwdata->SCRATCH_SIZE = num_clmblks * clmblkdst + gaps * 192;
			} else {					// clm=4 or more
				gaps = num_clmblks / 8;
				gwdata->SCRATCH_SIZE = num_clmblks * (clmblkdst + 64) + gaps * -64;
			}
		}
		else if (gwdata->cpu_flags & CPU_AVX) {		// AVX scratch area size
			int	pass1_size, pass1_chunks, gaps;
			// For small clms, AVX pads 64 bytes every 8 chunks (where pass1_chunks is the number of
			// cache lines that a pass 1 data set occupies).  For large clms, AVX pads 64 bytes every
			// chunk, and -64 bytes every 8 chunks.
			pass1_size = gwdata->PASS1_SIZE;
			pass1_chunks = pass1_size >> 3;
			if ((pass1_size == 384 && gwdata->NEGACYCLIC_FFT) ||
			    (pass1_size == 640 && gwdata->NEGACYCLIC_FFT) ||
			    (pass1_size == 1536 && gwdata->NEGACYCLIC_FFT)) { // Oddball pass 1 sizes
				gwdata->SCRATCH_SIZE = pass1_chunks * (gwdata->PASS1_CACHE_LINES * 64 + 64) - 64;
			} else if (gwdata->PASS1_CACHE_LINES == 4) {	// clm=1
				gaps = pass1_chunks / 8 - 1;
				gwdata->SCRATCH_SIZE = pass1_chunks * gwdata->PASS1_CACHE_LINES * 64 + gaps * 64;
			} else if (gwdata->PASS1_CACHE_LINES == 8) {	// clm=2
				gaps = pass1_chunks / 8 - 1;
				gwdata->SCRATCH_SIZE = pass1_chunks * gwdata->PASS1_CACHE_LINES * 64 + gaps * 192;
			} else {					// clm=4 or more
				gaps = pass1_chunks / 8;
				gwdata->SCRATCH_SIZE = pass1_chunks * (gwdata->PASS1_CACHE_LINES * 64 + 64) + gaps * -64;
			}
		} else if (gwdata->cpu_flags & CPU_SSE2) {	// SSE2 scratch area size
			int	pass1_chunks, gaps;
			// SSE2 pads 128 bytes every 8 chunks
			pass1_chunks = gwdata->PASS1_SIZE >> 2;
			gaps = pass1_chunks / 8 - 1;
			gwdata->SCRATCH_SIZE = pass1_chunks * gwdata->PASS1_CACHE_LINES * 64 + gaps * 128;
		} else {					// x87 scratch area size
			int	pass1_chunks, gaps;
			// x87 pads 64 bytes every 32 chunks
			pass1_chunks = gwdata->PASS1_SIZE >> 1;
			gaps = pass1_chunks / 32 - 1;
			if (gaps < 3) gaps = 0;
			gwdata->SCRATCH_SIZE = pass1_chunks * gwdata->PASS1_CACHE_LINES * 32 + gaps * 64;
		}
	} else
		gwdata->SCRATCH_SIZE = 0;

/* Calculate the gaps between data blocks.  This is used by addr_offset called from */
/* gwmap_to_estimated_size without a call to gwsetup.  This must match the calculations */
/* done in the set_FFT_constants macro in zmult.mac, ymult.mac, and xmult.mac */

	if (gwdata->cpu_flags & CPU_AVX512F) {
		gwdata->FOURKBGAPSIZE = 0;		/* BUG  No padding for now  */
		gwdata->PASS2GAPSIZE = 64;		/* BUG  64 bytes padding for now, might want zero for small pass 2 sizes */
		if (gwdata->PASS2_SIZE == 3072 || gwdata->PASS2_SIZE == 3584 || gwdata->PASS2_SIZE == 3840 || gwdata->PASS2_SIZE == 4096 ||
		    gwdata->PASS2_SIZE == 5120 || gwdata->PASS2_SIZE == 5376 || gwdata->PASS2_SIZE == 6144 || gwdata->PASS2_SIZE >= 7168) {
			gwdata->FOURKBGAPSIZE = 64;	/* Proven good choice for 7680*//* BUG  Try 128, others? */
			gwdata->PASS2GAPSIZE = -64;	/* Proven good choice for 7680,clm=2*/
							/* +64 was bad for 7680,clm=2 since 1951 cache lines mod 64 = 31  causing */
							/* 4KB overlaps when clm=2 reads 4 cache lines whereas 1949 mod 61 = 29 did not? */
							/* BUG  try 4kbgap = 128 to work better with hardware prefetcher's adjacent line try 64, others? */
		}
	}
	else if (gwdata->cpu_flags & CPU_AVX) {
		// Pass 2 sizes avoid the 4KB distance problem with 64, 128, or 192 pad bytes
		if (gwdata->PASS2_SIZE == 48 || gwdata->PASS2_SIZE == 64 || gwdata->PASS2_SIZE == 80 || gwdata->PASS2_SIZE == 192 || gwdata->PASS2_SIZE == 320)
			gwdata->FOURKBGAPSIZE = 0;
		if (gwdata->PASS2_SIZE == 256 || gwdata->PASS2_SIZE == 768 || gwdata->PASS2_SIZE == 2048 || gwdata->PASS2_SIZE == 2304 ||
		    gwdata->PASS2_SIZE == 3072 || gwdata->PASS2_SIZE == 3840 || gwdata->PASS2_SIZE == 4096 || gwdata->PASS2_SIZE == 5120 ||
		    gwdata->PASS2_SIZE == 6144 || gwdata->PASS2_SIZE == 6400 || gwdata->PASS2_SIZE == 8192 || gwdata->PASS2_SIZE == 9216 ||
		    gwdata->PASS2_SIZE == 10240 || gwdata->PASS2_SIZE == 12288 || gwdata->PASS2_SIZE == 15360 || gwdata->PASS2_SIZE == 16384 ||
		    gwdata->PASS2_SIZE == 20480 || gwdata->PASS2_SIZE == 25600)
			gwdata->FOURKBGAPSIZE = 64;
		if (gwdata->PASS2_SIZE == 1024 || gwdata->PASS2_SIZE == 1280 || gwdata->PASS2_SIZE == 1536 || gwdata->PASS2_SIZE == 2560 ||
		    gwdata->PASS2_SIZE == 4608 || gwdata->PASS2_SIZE == 7680 || gwdata->PASS2_SIZE == 12800)
			gwdata->FOURKBGAPSIZE = 128;
		// Make sure pass 2 block gapsize matches the computation of blkdst in ymult.mac
		if (gwdata->FOURKBGAPSIZE == 0)
			gwdata->PASS2GAPSIZE = 0;
		else if (gwdata->PASS2_SIZE == 2304 || gwdata->PASS2_SIZE == 9216)
			gwdata->PASS2GAPSIZE = 4*64;
		else if (gwdata->PASS2_SIZE * 2 * 8 / 4096 * gwdata->FOURKBGAPSIZE % 128 == 0)
			gwdata->PASS2GAPSIZE = -64;
		else
			gwdata->PASS2GAPSIZE = 0;
		// For one-pass FFTs, select the optimal padding frequency
		if (gwdata->PASS2_SIZE == 0) {
			if (gwdata->FFTLEN == 1536 || gwdata->FFTLEN == 2560)
				gwdata->FOURKBGAPSIZE = 16;		/* Pad every 1KB */
			else if (gwdata->FFTLEN == 1024 || gwdata->FFTLEN == 3072 || gwdata->FFTLEN == 5120)
				gwdata->FOURKBGAPSIZE = 32;		/* Pad every 2KB */
			else if (gwdata->FFTLEN == 2048 || gwdata->FFTLEN == 4096 || gwdata->FFTLEN == 6144 || gwdata->FFTLEN == 8192 ||
				 gwdata->FFTLEN == 10240 || gwdata->FFTLEN == 12288 || gwdata->FFTLEN == 16384 || gwdata->FFTLEN == 18432 ||
				 gwdata->FFTLEN == 20480 || gwdata->FFTLEN == 24576 || gwdata->FFTLEN == 32768)
				gwdata->FOURKBGAPSIZE = 64;		/* Pad every 4KB */
			else
				gwdata->FOURKBGAPSIZE = 0;		/* No padding */
		}
	} else {
		if (gwdata->PASS2_SIZE * 2 * 2 * 8 / 8192 * 128 % 256 == 0)
			gwdata->PASS2GAPSIZE = -128;
		else
			gwdata->PASS2GAPSIZE = 0;
	}

/* Copy or calculate various counts */

	asm_data = (struct gwasm_data *) gwdata->asm_data;
	if (asm_data != NULL) {
		if (gwdata->cpu_flags & CPU_AVX512F) {
			/* Copy the second pass routine ptr */
			struct gwasm_alt_jmptab *altjmptab = (struct gwasm_alt_jmptab *) jmptab;
			asm_data->u.zmm.ZMM_PASS2_ROUTINE = altjmptab->pass2_proc_ptr;

			if (gwdata->PASS1_SIZE == 0) {
				/* Count of sections for add, sub, addsub, zgw_carries_wpn in a one pass FFT */
				asm_data->addcount1 = gwdata->FFTLEN / 128;
			}
			else {
				/* Count of sections for add, sub, addsub, zgw_carries_wpn.  Value is pass 1 size over 16 doubles */
				asm_data->addcount1 = gwdata->PASS1_SIZE / 16;
			}
		}
		else if (gwdata->cpu_flags & CPU_AVX) {
			if (gwdata->PASS2_SIZE == 0) {
				if (gwdata->FOURKBGAPSIZE) {				/* Pad every 1KB, 2KB, or 4KB */
					asm_data->count1 = gwdata->FOURKBGAPSIZE;	/* Cache lines before a padding occurs */
					asm_data->normcount1 = gwdata->FFTLEN / 32 / gwdata->FOURKBGAPSIZE; /* Number of padding groups in each of the 4 sections */
					asm_data->count2 = asm_data->count1 / 2;	/* Counter for add/sub quick functions */
					asm_data->addcount1 = asm_data->normcount1 * 4;	/* Number of add/sub quick padding groups */
				} else {						/* No padding */
					asm_data->count1 = gwdata->FFTLEN / 32;		/* Cache lines in a section */
					asm_data->normcount1 = 0;			/* Number of padding groups in each of the 4 sections */
					asm_data->count2 = gwdata->FFTLEN / 16;		/* Counter for add/sub quick functions (4 AVX words processed at a time) */
					asm_data->addcount1 = 1;			/* Number of add/sub quick padding groups */
				}
			}
			else {
				/* Count of sections for add, sub, addsub, ygw_carries_wpn */
				asm_data->addcount1 = (gwdata->FFTLEN / 2) / (gwdata->PASS2_SIZE * 4);
				/* NOTE: more counts (count2, count3) for 2-pass FFTs are set in yr4dwpn_build_pass1_table */

				/* Copy the second pass routine ptr.  This is only present when first pass code is shared. */
				if (jmptab->flags & 0x40000000) {
					struct gwasm_alt_jmptab *altjmptab = (struct gwasm_alt_jmptab *) jmptab;
					asm_data->u.ymm.YMM_PASS2_ROUTINE = altjmptab->pass2_proc_ptr;
				}
			}
		} else if (gwdata->cpu_flags & CPU_SSE2) {
			if (gwdata->PASS2_SIZE == 0) {
				const struct gwasm_jmptab *last_jmptab;
				/* Copy 7 counts for one pass SSE2 FFTs.  The counts are after the last jmptab entry */
				last_jmptab = LAST_JMPTAB (jmptab);
				asm_data->addcount1 = last_jmptab->counts[0];
				asm_data->normcount1 = last_jmptab->counts[1];
				asm_data->count1 = last_jmptab->counts[2];
				asm_data->count2 = last_jmptab->counts[3];
				asm_data->count3 = last_jmptab->counts[4];
				asm_data->count4 = last_jmptab->counts[5];
				asm_data->count5 = last_jmptab->counts[6];
			} else if (gwdata->FFT_TYPE == FFT_TYPE_HOME_GROWN) {
				int	pfa, pfa_shift;
				/* Count of pass 2 sections */
				asm_data->addcount1 = gwdata->PASS1_SIZE / 4;
				/* Count of complex pass 2 sections */
				asm_data->count1 = asm_data->addcount1;
				if (gwdata->ZERO_PADDED_FFT || c < 0) asm_data->count1--;
				/* Up to three section counts for gw_carries.  Examples: */
				/* 320 pass 2 sections: (256 << 11) + 64 */
				/* 384 pass 2 sections: 384 */
				/* 448 pass 2 sections: (256 << 22) + (128 << 11) + 64 */
				/* 512 pass 2 sections: 512 */
				for (pfa = asm_data->addcount1, pfa_shift = 0;
				     pfa > 8;
				     pfa >>= 1, pfa_shift++);
				if (pfa == 5)
					asm_data->count3 = ((4 << 11) + 1) << pfa_shift;
				else if (pfa == 7)
					asm_data->count3 = ((4 << 22) + (2 << 11) + 1) << pfa_shift;
				else
					asm_data->count3 = asm_data->addcount1;
			} else {
				/* Count of pass 2 sections */
				asm_data->addcount1 = gwdata->PASS1_SIZE / 4;
				/* Count of complex pass 2 sections */
				asm_data->count1 = asm_data->addcount1;
				if (gwdata->ZERO_PADDED_FFT || c < 0) asm_data->count1--;
				/* Only one section counts for gw_carries */
				asm_data->count3 = asm_data->addcount1;
			}
		} else {
			if (gwdata->PASS2_SIZE == 0) {
				/* 5 counts for one pass x87 FFTs */
				asm_data->count1 = jmptab->counts[0];
				asm_data->count2 = jmptab->counts[1];
				asm_data->count3 = jmptab->counts[2];
				asm_data->count4 = jmptab->counts[3];
				asm_data->count5 = jmptab->counts[4];
			} else {
				/* Count of pass 2 sections */
				asm_data->addcount1 = gwdata->PASS1_SIZE / 2;
				/* Count of complex pass 2 sections */
				asm_data->count1 = asm_data->addcount1;
				if (gwdata->ZERO_PADDED_FFT || c < 0) asm_data->count1--;
			}
		}
	}

/* Compute alignment for allocated data.  Strangely enough assembly prefetching works best in pass 1 on a P4 if the data is allocated on an odd cache line. */
/* An optimal 31 of the 32 cache lines on a 4KB page will be prefetchable.  Page aligned data would only prefetch 28 of the 32 cache lines. */

	if (gwdata->cpu_flags & CPU_AVX512F) {
		if (gwdata->PASS1_SIZE == 0) {		/* One pass */
			gwdata->GW_ALIGNMENT = 128;	/* Not tested yet */
			gwdata->GW_ALIGNMENT_MOD = 0;
		} else {				/* Two passes */
			gwdata->GW_ALIGNMENT = 1024;	/* Clmblkdst (up to 8) */
			gwdata->GW_ALIGNMENT_MOD = 0;	/* Not tested yet */
		}
	}
	else if (gwdata->cpu_flags & CPU_AVX) {
		if (gwdata->PASS2_SIZE == 0) {		/* One pass */
			gwdata->GW_ALIGNMENT = 64;	/* Sandy Bridge cache line alignment */
			gwdata->GW_ALIGNMENT_MOD = 0;
		} else if (gwdata->SCRATCH_SIZE == 0) {	/* Small two passes */
			gwdata->GW_ALIGNMENT = 4096;	/* Page alignment */
			gwdata->GW_ALIGNMENT_MOD = 0;
		} else {				/* Large two passes */
			gwdata->GW_ALIGNMENT = 1024;	/* Clmblkdst (up to 8) */
			gwdata->GW_ALIGNMENT_MOD = 64;	/* + 1 cache line */
		}
	}
	else if (gwdata->cpu_flags & CPU_SSE2) {
		if (gwdata->PASS2_SIZE == 0) {		/* One pass */
			gwdata->GW_ALIGNMENT = 128;	/* P4 cache line alignment */
			gwdata->GW_ALIGNMENT_MOD = 0;
		} else if (gwdata->SCRATCH_SIZE == 0) {	/* Small two passes */
			gwdata->GW_ALIGNMENT = 4096;	/* Page alignment */
			gwdata->GW_ALIGNMENT_MOD = 0;
		} else {				/* Large two passes */
			gwdata->GW_ALIGNMENT = 1024;	/* Clmblkdst (up to 8) */
			gwdata->GW_ALIGNMENT_MOD = 128; /* + 1 cache line */
		}
	} else {
		if (gwdata->PASS2_SIZE == 0)		/* One pass */
			gwdata->GW_ALIGNMENT = 128;	/* P4 cache line alignment */
		else					/* Two passes */
			gwdata->GW_ALIGNMENT = 4096;	/* Page alignment */
		gwdata->GW_ALIGNMENT_MOD = 0;
	}

/* Compute FFT data size.  Needed for allocating gwnums. */

	gwdata->datasize = addr_offset (gwdata, gwdata->FFTLEN - 1) + sizeof (double);

/* All done */

	return (0);
}

/* Code to manage sharing sin/cos data where possible amongst several gwnum callers */

#define FIXED_PASS1_SINCOS_DATA			1
#define PASS2_REAL_SINCOS_DATA			2
#define PASS2_COMPLEX_SINCOS_DATA		3

struct shareable_sincos_data {
	struct shareable_sincos_data *next;	/* Next in linked list of shareable data blocks */
	double	*data;				/* Shareable data */
	size_t	data_size;			/* Size of the shareable data */
	int	use_count;			/* Count of times data is shared */
};
struct shareable_sincos_data *shareable_data = NULL;  /* Linked list of shareable data blocks */
gwmutex	shareable_lock;				/* This mutex limits one caller into sharing routines */
int	shareable_lock_initialized = FALSE;	/* Whether shareable mutex is initialized */

/* Share sin/cos data where possible amongst several gwnum callers */

double *share_sincos_data (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	int	table_type,	/* Type of the sin/cos table defined above */
	double *table,		/* Sin/cos data to share */
	size_t	table_size)	/* Size of the sin/cos data */
{
#ifdef SHARE_SINCOS_DATA
	struct shareable_sincos_data *p;	/* Ptr to a shareable data block */

/* No need to share zero-sized tables */

	if (table_size == 0) return (table);

/* Initialize the mutex if necessary, then grab the lock */

	if (!shareable_lock_initialized) {
		gwmutex_init (&shareable_lock);
		shareable_lock_initialized = TRUE;
	}
	gwmutex_lock (&shareable_lock);

/* Look through the list of shareable blocks looking for a match.  If we find a match, use it! */

	for (p = shareable_data; p != NULL; p = p->next) {
		if (p->data_size == table_size && !memcmp (p->data, table, table_size)) {
			p->use_count++;
			gwmutex_unlock (&shareable_lock);
			return (p->data);
		}
	}

/* Unfortunately, no match.  Copy the data so that a future gwnum caller can share the data */

	p = (struct shareable_sincos_data *) malloc (sizeof (struct shareable_sincos_data));
	if (p == NULL) {
		gwmutex_unlock (&shareable_lock);
		return (table);
	}
	p->data = (double *) aligned_malloc (table_size, 4096);
	if (p->data == NULL) {
		free (p);
		gwmutex_unlock (&shareable_lock);
		return (table);
	}
	memcpy (p->data, table, table_size);
	p->data_size = table_size;
	p->use_count = 1;
	p->next = shareable_data;
	shareable_data = p;
	gwmutex_unlock (&shareable_lock);
	return (p->data);
#else
	return (table);
#endif
}

/* Free shared sin/cos data */

void unshare_sincos_data (
	double *table)		/* Possibly shared sin/cos data */
{
#ifdef SHARE_SINCOS_DATA
	struct shareable_sincos_data *p;		/* Ptr to a shareable data block */
	struct shareable_sincos_data **ptr_to_p;	/* Linked list pointer to patch */

/* Ignore NULL table.  Should only happen when there are errors during gwsetup. */

	if (table == NULL) return;

/* Grab the lock */

	if (!shareable_lock_initialized) return;
	gwmutex_lock (&shareable_lock);

/* Look through the list of shareable blocks to find this shareable block */
/* Decrement the use count and when it reaches zero, free the memory */

	for (ptr_to_p = &shareable_data; (p = *ptr_to_p) != NULL; ptr_to_p = &p->next) {
		if (p->data != table) continue;
		if (--p->use_count == 0) {
			aligned_free (p->data);
			*ptr_to_p = p->next;
			free (p);
		}
		break;
	}

/* Free the lock and return */

	gwmutex_unlock (&shareable_lock);
#endif
}

/* Initialize gwhandle for a future gwsetup call. */
/* The gwinit function has been superceeded by gwinit2.  By passing in the */
/* version number we can verify the caller used the same gwnum.h file as the */
/* one he eventually links with.  The sizeof (gwhandle) structure is used */
/* to verify he compiles with the same structure alignment options that */
/* were used when compiling gwnum.c.  For compatibility with existing code */
/* we delay reporting any compatibility problems until gwsetup is called. */

void gwinit2 (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	int	struct_size,	/* Size of the gwdata structure */
	const char *version_string)
{

/* See if caller is using the same gwnum.h file that was used when */
/* this file was compiled.  Checking structure size also verifies he */
/* used the same compiler switches - especially regarding alignment. */
/* As a hack, we use GWERROR to record delayed error messages. */

	if (strcmp (version_string, GWNUM_VERSION)) {
		gwdata->GWERROR = GWERROR_VERSION_MISMATCH;
		return;
	}
	if (struct_size != sizeof (gwhandle)) {
		gwdata->GWERROR = GWERROR_STRUCT_SIZE_MISMATCH;
		return;
	}

/* Initialize gwhandle structure with the default values */

	memset (gwdata, 0, sizeof (gwhandle));
	gwdata->safety_margin = 0.0f;
	gwdata->maxmulbyconst = 3;
	gwdata->minimum_fftlen = 0;
	gwdata->larger_fftlen_count = 0;
	gwdata->num_threads = 1;
	gwdata->force_general_mod = 0;
	gwdata->use_irrational_general_mod = 0;
	gwdata->use_large_pages = 0;
	gwdata->use_benchmarks = 1;
	gwdata->gwnum_max_free_count = 10;
	gwdata->scramble_arrays = 1;
	gwdata->mem_needed = GWINIT_WAS_CALLED_VALUE;	/* Special code checked by gwsetup to ensure gwinit was called */

/* Init structure that allows giants and gwnum code to share allocated memory */

	init_ghandle (&gwdata->gdata);
	gwdata->gdata.allocate = &gwgiantalloc;
	gwdata->gdata.free = &gwgiantfree;
	gwdata->gdata.deallocate = &gwgiantdealloc;
	gwdata->gdata.handle = (void *) gwdata;

/* If CPU type and speed have not been initialized by the caller, do so now. */

	if (CPU_FLAGS == 0 && CPU_SPEED == 0.0) {
		guessCpuType ();
		guessCpuSpeed ();
	}
	gwdata->cpu_flags = CPU_FLAGS;

/* We have not and will not write FMA3 or AVX-512 FFTs for 32-bit OSes */

#ifndef X86_64
	gwdata->cpu_flags &= ~(CPU_AVX512F | CPU_FMA3);
#endif

/* FMA3 FFTs require both AVX and FMA3 instructions.  This will always be the case when CPUID */
/* is queried.  However, prime95 has an option to turn off just the AVX bit with CpuSupportsAVX=0. */
/* Handle, this oddball case by also turning off FMA3. */

	if (! (gwdata->cpu_flags & CPU_AVX)) gwdata->cpu_flags &= ~CPU_FMA3;

/* AMD Bulldozer is faster using SSE2 rather than AVX. */
/* Why do we do this here when calculate_bif selects K10 FFTs???  Is it so that gwnum_map_to_timing and other */
/* informational routines return more accurate information?   Since the code below seems to work, leave it as is. */
/* 2019: A user reports that 3rd & 4th generation Bulldozer (Steamroller & Excavator) is better at AVX. */
/* I'm confident that 1st and 2nd are dismal (Bulldozer and Piledriver).  Consequently the code below was changed to */
/* check the extended model number using the chart at https://en.wikipedia.org/wiki/List_of_AMD_CPU_microarchitectures */

	if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_BULLDOZER && ((CPU_SIGNATURE >> 16) & 0xF) < 3)
		gwdata->cpu_flags &= ~(CPU_AVX512F | CPU_AVX | CPU_FMA3);

/* Read the benchmark data from gwnum.txt INI file.  Pass in cpu_flags to aid in determining which bench data to ignore from older gwnum versions. */

	gwbench_read_data (gwdata->cpu_flags);
}

/* Allocate memory and initialize assembly code for arithmetic */
/* modulo k*b^n+c */

int gwsetup (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. */
{
	int	gcd, error_code, setup_completed;
	double	orig_k;
	unsigned long orig_n;

/* Make sure gwinit was called */

	if (gwdata->mem_needed != GWINIT_WAS_CALLED_VALUE) return (GWERROR_NO_INIT);

/* Return delayed errors from gwinit2 */

	if (gwdata->GWERROR) return (gwdata->GWERROR);

/* Sanity check the k,b,n,c values */

	if (k < 1.0) return (GWERROR_K_TOO_SMALL);
	if (k > 9007199254740991.0) return (GWERROR_K_TOO_LARGE);
	if (gwdata->minimum_fftlen == 0) {
		if (gwdata->cpu_flags & CPU_AVX512F) {
			if (log2(b) * (double) n > MAX_PRIME_AVX512) return (GWERROR_TOO_LARGE);
		} else if (gwdata->cpu_flags & CPU_FMA3) {
			if (log2(b) * (double) n > MAX_PRIME_FMA3) return (GWERROR_TOO_LARGE);
		} else if (gwdata->cpu_flags & CPU_AVX) {
			if (log2(b) * (double) n > MAX_PRIME_AVX) return (GWERROR_TOO_LARGE);
		} else if (gwdata->cpu_flags & CPU_SSE2) {
			if (log2(b) * (double) n > MAX_PRIME_SSE2) return (GWERROR_TOO_LARGE);
		} else {
			if (log2(b) * (double) n > MAX_PRIME) return (GWERROR_TOO_LARGE);
		}
	}
	if ((k == 1.0 && n == 0 && c == 0) || (c < 0 && n * log ((double) b) + log (k) <= log ((double) 1-c)))
		return (GWERROR_TOO_SMALL);

/* Init */

	setup_completed = FALSE;
	orig_k = k;
	orig_n = n;

/* Our code fails if k is a power of b.  For example, 3481*59^805-1 which */
/* equals 59^807-1.  I think this is because gwfft_base(FFTLEN) is off by one */
/* because even quad-precision floats won't calculate FFTLEN * num_b_per_word */
/* correctly.  There is an easy fix, if k is divisible by b we divide k by b */
/* and add one to n. */

	while (k > 1.0 && b > 1 && fmod (k, (double) b) == 0.0) {
		k = k / (double) b;
		n = n + 1;
	}

/* Our code fast code fails if k and c are not relatively prime.  This */
/* is because we cannot calculate 1/k.  Although the user shouldn't call */
/* us with this case, we handle it anyway by reverting to the slow general */
/* purpose multiply routines. */

	if (c == 0)
		gcd = 0;
	else if (k == 1.0 || labs (c) == 1)
		gcd = 1;
	else {
		stackgiant(kg,2);
		stackgiant(cg,2);
		dbltog (k, kg);
		itog (labs (c), cg);
		gcdg (kg, cg);
		gcd = cg->n[0];
	}

/* Call the internal setup routine when we can.  Gcd (k, c) must be 1, */
/* k * mulbyconst and c * mulbyconst cannot be too large.  Also, the FFT */
/* code has bugs when there are too few bits per FFT.  Rather than make */
/* difficult fixes we simply force these small numbers to use the generic */
/* reduction.  In truth, the caller should use a different math package for */
/* these small numbers. */

	if (gcd == 1 &&
	    k * gwdata->maxmulbyconst <= MAX_ZEROPAD_K &&
	    labs (c) * gwdata->maxmulbyconst <= MAX_ZEROPAD_C &&
	    log2(b) * (double) n >= 350.0 &&
	    (b == 2 || (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2))) &&
	    !gwdata->force_general_mod) {
		error_code = internal_gwsetup (gwdata, k, b, n, c);
		if (error_code == 0) setup_completed = TRUE;
		else if (b == 2) return (error_code);
		gwdata->GENERAL_MOD = FALSE;
		gwdata->GENERAL_MMGW_MOD = FALSE;
	}

/* Emulate k not relatively prime to c, small n values, and */
/* large k or c values with a call to the general purpose modulo setup code. */

	if (!setup_completed) {
		/* If we've already copied the modulus, use it.  For example, gwsetup_general_mod_giant on (2^313+1)/3 will call this routine */
		/* to try an IBDWT on 2^313+1.  This number is too small and we need to revert back to a general mod on (2^313+1)/3. */
		if (gwdata->GW_MODULUS != NULL) {
			if (gwdata->force_general_mod == 0) gwdata->force_general_mod = 1;
			error_code = gwsetup_general_mod_giant (gwdata, gwdata->GW_MODULUS);
			if (error_code) return (error_code);
		} else {
			double	bits;
			giant	g;
			bits = (double) n * log2 (b);
			g = allocgiant (((unsigned long) bits >> 5) + 4);
			if (g == NULL) return (GWERROR_MALLOC);
			ultog (b, g);
			power (g, n);
			dblmulg (k, g);
			iaddg (c, g);
			if (gwdata->force_general_mod == 0) gwdata->force_general_mod = 1;
			error_code = gwsetup_general_mod_giant (gwdata, g);
			free (g);
			if (error_code) return (error_code);
		}
	}

/* For future messages, format the input number as a string */

	gw_as_string (gwdata->GWSTRING_REP, orig_k, b, orig_n, c);

/* Return success */

	return (0);
}

/* These setup routines are for operations modulo an arbitrary binary number. */
/* This is three times slower than the special forms above. */

int gwsetup_general_mod (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	const uint32_t *array,	/* The modulus as an array of 32-bit values */
	uint32_t arraylen)	/* Number of values in the array */
{
	giantstruct tmp;
	tmp.sign = arraylen;
	tmp.n = (uint32_t *) array;
	while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
	return (gwsetup_general_mod_giant (gwdata, &tmp));
}

int gwsetup_general_mod_64 (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	const uint64_t *array,	/* The modulus as an array of 64-bit values */
	uint64_t arraylen)	/* Number of values in the array */
{
	giantstruct tmp;
	tmp.sign = (int) arraylen * 2;
	tmp.n = (uint32_t *) array;
	while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
	return (gwsetup_general_mod_giant (gwdata, &tmp));
}

/* Setup the FFT code for generic Barrett reduction */

int gwsetup_general_Barrett_mod_giant (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	giant	g,		/* The modulus */
	unsigned long d)	/* Multiplier for the modulus */
{
	unsigned long bits;	/* Bit length of modulus */
	unsigned long n;
	unsigned long safety_bits;
	const struct gwasm_jmptab *info;
	int	error_code;
	unsigned long fftlen, max_exponent, desired_n;
	giant	modified_modulus, tmp;

/* Init */

	bits = bitlen (g);

/* If we need to multiply the modulus by a small d value, do so here */

	if (d != 1) {
		modified_modulus = allocgiant ((bits >> 5) + 2);
		if (modified_modulus == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}
		gtog (g, modified_modulus);
		ulmulg (d, modified_modulus);
		g = modified_modulus;
		bits = bitlen (g);
	} else
		modified_modulus = NULL;

/* We will need twice the number of input bits plus some padding */

	n = bits + bits + 128;

/* Setup the FFT code in much the same way that gwsetup_without_mod does. */
/* Unless the user insists, we try for an integral number of bits per word. */
/* There are pathological bit patterns that generate huge roundoff errors. */
/* For example, if we test (10^828809-1)/9 and put exactly 18 bits into */
/* each FFT word, then every FFT word in GW_MODULUS_FFT will contain the */
/* same value!  Not exactly, the random data our FFTs require for small */
/* roundoff errors.  Thus, the caller may need to insist we use an */
/* irrational FFT on occasion. */

/* Call gwinfo and have it figure out the FFT length to use.  Since we zero the upper half of FFT input data, the FFT outputs will be smaller. */
/* This lets us get about another 0.15 bits per input word (data from Pavel Atnashev's primorial search). */

	gwdata->safety_margin -= 0.15f;
	error_code = gwinfo (gwdata, 1.0, 2, n, -1);
	gwdata->safety_margin += 0.15f;
	if (error_code) return (error_code);
	info = gwdata->jmptab;
	fftlen = info->fftlen;
	max_exponent = adjusted_max_exponent (gwdata, info);

/* Our FFTs don't handle cases where there are few bits per word because carries must be */
/* propagated over too many words.  Arbitrarily insist that n is at least 12 * fftlen. */

	if (n < 12 * fftlen) n = 12 * fftlen;

/* If the caller requests rational FFTs (the default as they are a few percent faster due to no FFT weights). */
/* If possible, increase n to the next multiple of FFT length. */

	if (!gwdata->use_irrational_general_mod) {
		desired_n = round_up_to_multiple_of (n, fftlen);
		if (desired_n < max_exponent) n = desired_n;
	}

/* If the caller requests irrational FFTs, then make sure the bits per FFT word will distribute the big and little words of the modulus semi-randomly. */
/* For example, in the (10^828809-1)/9 case above, if bits-per-word is 18.5 or 18.25 you will still get non-random patterns in the FFT words. */

	else {
		double	prime_number, bits_per_word;

/* Round bits_per_word up to the next half-multiple of 1/prime_number */

		prime_number = 53.0;
		bits_per_word = (double) n / (double) fftlen;
		bits_per_word = (ceil (bits_per_word * prime_number) + 0.5)/ prime_number;

/* If possible, use the n associated with the just-computed bits-per-word */

		desired_n = (unsigned long) ceil (bits_per_word * (double) fftlen);
		if (desired_n < max_exponent) n = desired_n;
	}

/* If possible, increase n to the next multiple of FFT length.  The extra bits allow gwsmallmul to avoid emulate_mod calls more often. */
/* We hope the 0.15 safety_limit increase above will avoid getting too close to the FFT limit as many users of this library turn on error */
/* checking (slower) when near the FFT limit.  If that doesn't work, try adding a half FFT length instead. */

	if (n + fftlen < max_exponent) n = n + fftlen;
	else if (gwdata->use_irrational_general_mod && n + fftlen / 2 < max_exponent) n = n + fftlen / 2;

/* Now setup the assembly code.  Make sure we select the same FFT length chosen above (necessary  */

	gwdata->safety_margin -= 0.15f;
	int saved_minimum_fftlen = gwdata->minimum_fftlen;		// though not necessary, remember this option
	int saved_larger_fftlen_count = gwdata->larger_fftlen_count;	// though not necessary, remember this option
	gwdata->minimum_fftlen = fftlen;				// force use of the already found fft length
	gwdata->larger_fftlen_count = 0;				// gwinfo above already found the next larger fft length
	error_code = internal_gwsetup (gwdata, 1.0, 2, n, -1);
	gwdata->larger_fftlen_count = saved_larger_fftlen_count;	// though not necessary, restore this option to its original setting
	gwdata->minimum_fftlen = saved_minimum_fftlen;			// though not necessary, restore this option to its original setting
	gwdata->safety_margin += 0.15f;
	if (error_code) return (error_code);

// BUG - setting the bit_length to the modulus size will break gwtogiant.
// we need a better/more-consistent way of dealing with the various 
// needed bit_lengths.  Also, PFGW should not be reading the bit_length
// value in integer.cpp.
//	gwdata->bit_length = bits;

/* Allocate memory for an FFTed copy of the modulus. */

	if (!gwdata->information_only) {
		gwdata->GW_MODULUS_FFT = gwalloc_internal (gwdata);
		if (gwdata->GW_MODULUS_FFT == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}
		gianttogw (gwdata, g, gwdata->GW_MODULUS_FFT);
		gwfft (gwdata, gwdata->GW_MODULUS_FFT, gwdata->GW_MODULUS_FFT);

/* Calculate number of words to zero during the copy prior to calculating the quotient. */

		gwdata->GW_ZEROWORDSLOW = (unsigned long) floor ((double) bits / gwdata->avg_num_b_per_word);

/* A quick emulate_mod refresher: */
/* 1) The maximum size of a quotient is gwdata->n/2 bits due to the */
/*    zeroing of high words in the normalization routine.  Obviously the */
/*    reciprocal needs to be accurate to at least gwdata->n/2 bits. */
/* 2) So that the quotient doesn't overflow, the maximum size of a value */
/*    entering emulate_mod is gwdata->n/2+bits bits */
/* 3) So that gwsquare and gwmul don't send emulate_mod a value that is */
/*    too large, the maximum input to these routines should be (allowing */
/*    for an 8 bit mulbyconstant) is (gwdata->n/2+bits-8)/2 bits.  This is */
/*    used by gwsmallmul to know when an emulate_mod is required */
/* 4) We cannot quite push to the limits calculated above because we have to */
/*    make sure the quotient calculation does not produce more than gwdata->n */
/*    bits of result -- otherwise the *high* order bits of the quotient will be */
/*    corrupted.  We allow ourselves a small safety margin by decreasing the */
/*    number of reciprocal bits calculated.  The safety margin must be larger */
/*    than the number of unzeroed bits caused by using the floor function in */
/*    calculating GW_ZEROWORDSLOW. */

		safety_bits = bits - (unsigned long) ((double) gwdata->GW_ZEROWORDSLOW * gwdata->avg_num_b_per_word) + 3;

/* Precompute the reciprocal of the modified modulus. */

		gwdata->GW_RECIP_FFT = gwalloc_internal (gwdata);
		if (gwdata->GW_RECIP_FFT == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}
		tmp = allocgiant ((gwdata->n >> 5) + 1);
		if (tmp == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}
		itog (1, tmp);
		gshiftleft (bits + gwdata->n / 2 - safety_bits, tmp);
		divg (g, tmp);		/* computes gwdata->n/2-safety_margin+1 bits of reciprocal */
		gshiftleft (gwdata->n - (bits + gwdata->n / 2 - safety_bits), tmp);
					/* shift so gwmul routines wrap quotient to lower end of fft */
		gianttogw (gwdata, tmp, gwdata->GW_RECIP_FFT);
		gwfft (gwdata, gwdata->GW_RECIP_FFT, gwdata->GW_RECIP_FFT);
		free (tmp);
		free (modified_modulus);

/* Calculate the maximum allowable size of a number used as input */
/* to gwmul.  We will make sure gwsmallmul does not generate any */
/* results bigger than this. */

		gwdata->GW_GEN_MOD_MAX = (unsigned long) floor ((double)((gwdata->n/2-safety_bits+bits-8)/2) / gwdata->avg_num_b_per_word);
		gwdata->GW_GEN_MOD_MAX_OFFSET = addr_offset (gwdata, gwdata->GW_GEN_MOD_MAX-1);

/* Set flag indicating general-purpose modulo operations are in force */

		gwdata->GENERAL_MOD = TRUE;
		gwdata->GENERAL_MMGW_MOD = FALSE;
	}

/* Create dummy string representation. Calling gtoc to get the first */
/* several digits would be better, but it is too slow. */

	sprintf (gwdata->GWSTRING_REP, "A %ld-bit number", bits);

/* Return success */

	return (0);
}

/* Setup the FFT code for Montgomery-McLaughlin-Gallot-Woltman generic reduction (if possible) */

int gwsetup_general_mod_giant (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	giant	N)		/* The modulus */
{
	giant	safe_N;			/* Small prime times modulus to avoid pathological bit patterns */
	unsigned long bits, safe_bits;	/* Bit length of modulus */
	int	convertible;		/* Value can be converted to (k*2^n+c)/d */
	double	k;
	unsigned long n;
	signed long c;
	unsigned long d;
	int	error_code;
	unsigned long fftlen;
	gwhandle *cyclic_gwdata, *negacyclic_gwdata;

/* Make sure gwinit was called */

	if (gwdata->mem_needed != GWINIT_WAS_CALLED_VALUE) return (GWERROR_NO_INIT);

/* Return delayed errors from gwinit2 */

	if (gwdata->GWERROR) return (gwdata->GWERROR);

/* Init */

	bits = bitlen (N);

/* Examine the giant to see if it is a (k*2^n+c)/d value that we can better optimize. */
/* Also detect nasty bit patterns, like Phi (82730,2), where multiplying by a small d results in a less nasty bit pattern for the modulus. */

	d = 1;
	convertible = (!gwdata->force_general_mod && bits > 300 && convert_giant_to_k2ncd (N, &k, &n, &c, &d));

/* Copy the modulus except for convertible k*2^n+c values */

	if (!convertible || d != 1) {
		/* Yuk, we may already have saved the modulus.  For example, (2^313+1)/3 will come through here and save the modulus. But gwsetup of 2^313+1 */
		/* is too small for IBDWT, so this routine is recursively called.  We cannot reallocate because that will cause a memory leak. */
		if (gwdata->GW_MODULUS == NULL) {
			gwdata->GW_MODULUS = allocgiant ((bits >> 5) + 1);
			if (gwdata->GW_MODULUS == NULL) {
				gwdone (gwdata);
				return (GWERROR_MALLOC);
			}
			gtog (N, gwdata->GW_MODULUS);
		}
	}

/* Setup for values we are converting to use faster k*2^n+c FFTs. */

	if (convertible) {
		error_code = gwsetup (gwdata, k, 2, n, c);
		if (error_code) return (error_code);
		if (d != 1) {
			char	buf[60];
			strcpy (buf, gwdata->GWSTRING_REP);
			if (isdigit (buf[0])) sprintf (gwdata->GWSTRING_REP, "(%s)/%lu", buf, d);
			else sprintf (gwdata->GWSTRING_REP, "%s/%lu", buf, d);
		}
		return (0);
	}

/* Multiply the modulus by a 29-bit prime number to reduce impact of pathological bit patterns in the original modulus */

	if (1) {			// I may provide a gwnum option to turn this feature off
		safe_N = allocgiant ((bits >> 5) + 2);
		gtog (N, safe_N);
		imulg (400000009, safe_N);
		safe_bits = bitlen (safe_N);
	} else {
		safe_N = N;
		safe_bits = bits;
	}

/* Force Barrett reduction if so requested or N is even (can't later do divide by 2) or N is small (308 bits) as Barrett */
/* using a length 32 FFT should be faster than Montgomery using 2 length 32 FFTs. */

	if (gwdata->force_general_mod == 2 || (N->n[0] & 1) == 0 || safe_bits <= 308) {
		if (safe_N != N) free (safe_N);
		return (gwsetup_general_Barrett_mod_giant (gwdata, N, d));
	}

/* Setup the FFT code in much the same way that gwsetup_without_mod does.  Unless the user insists, we try for an integral number of bits per word. */
/* There are pathological bit patterns that generate huge roundoff errors.  For example, if we test (10^828809-1)/9 and put exactly 18 bits into each */
/* FFT word, then every FFT word in GW_MODULUS_FFT will contain the same value!  Not exactly, the random data our FFTs require for small roundoff errors. */
/* Thus, the caller may need to insist we use an irrational FFT on occasion. */

/* Search for an acceptable FFT length that has both cyclic and negacyclic FFT implementations. */

	gwdata->use_benchmarks = FALSE;					// Benchmarking MMGW generic reduction is not supported
	int saved_minimum_fftlen = gwdata->minimum_fftlen;		// though not necessary, remember this option
	int saved_larger_fftlen_count = gwdata->larger_fftlen_count;	// though not necessary, remember this option
	int larger_fftlen_count = saved_larger_fftlen_count;		// Counter to handle larger fft length within the loop below
	gwdata->larger_fftlen_count = 0;
	for ( ; ; gwdata->minimum_fftlen = fftlen + 1) {
		// We need FFTs that can handle the number of input bits plus some padding, especially if polymult may be used.  The value of 128 needs study.
		n = safe_bits + 128;

		// Find next potential FFT length.  Decrease the safety margin as we will likely use a rational FFT which has lower round off errors.
		gwdata->safety_margin -= 0.15f;
		gwdata->required_pass2_size = 0;			// Allow any pass 2 size
		error_code = gwinfo (gwdata, 1.0, 2, n, 1);
		gwdata->safety_margin += 0.15f;
		if (error_code) return (error_code);

		// Get the potential FFT length and maximum exponent the FFT length can handle
		const struct gwasm_jmptab *info = gwdata->jmptab;
		fftlen = info->fftlen;
		unsigned long max_exponent = adjusted_max_exponent (gwdata, info);

		// FFTs don't handle cases where there are few bits per word because carries must be propagated over too many words.
		// Arbitrarily insist that n is at least 12 * fftlen.
		if (n < 12 * fftlen) n = 12 * fftlen;

		// MMGW requires unweighted rational FFTs
		n = round_up_to_multiple_of (n, fftlen);
		if (n > max_exponent) continue;

		// Verify that cyclic and negacyclic FFTs are available for this (n, fftlen) combination
		gwdata->minimum_fftlen = fftlen;					// Force selection of the already found fft length
		gwdata->required_pass2_size = gwdata->PASS2_SIZE + gwdata->ARCH;	// Force negacyclic and cyclic FFTs to use same pass 2 size & architecture
		error_code = gwinfo (gwdata, 1.0, 2, n, 1);
		if (error_code) return (error_code);
		if (gwdata->jmptab->fftlen != fftlen) continue;
		error_code = gwinfo (gwdata, 1.0, 2, n, -1);
		if (error_code == GWERROR_TOO_LARGE) continue;				// This happens when no matching pass 2 size found
		if (error_code) return (error_code);
		if (gwdata->jmptab->fftlen != fftlen) continue;

		// Check larger_fftlen_count here.  If zero, we've found our desired fft length
		if (larger_fftlen_count == 0) break;
		larger_fftlen_count--;
	}

	// Need the inverse of N mod R, later FFTed for future cyclic multiplications.  Yves calls this Np_R.
	giant R = allocgiant (2 * (n >> 5) + 2);
	giant Np = allocgiant ((n >> 5) + 2);
	if (R == NULL || Np == NULL) { gwdone (gwdata); return (GWERROR_MALLOC); }
	itog (1, R); gshiftleft (n, R); sladdg (-1, R);			// 2^n-1
	gtog (safe_N, Np);
	invg (R, Np);							// Np = 1/N mod R

	// R and N may not be relatively prime which is a requirement of MMGW algorithm!  Fall back to the older Barrett reduction.
	if (Np->sign <= 0) {
		Np->sign = -Np->sign; modg (Np, R); if (!isZero(R)) { gwdone (gwdata); return (GWERROR_INTERNAL+12); } // Sanity check invg's results
		free (Np); free (R);
		gwdata->larger_fftlen_count = saved_larger_fftlen_count;	// Restore this option to its original setting
		gwdata->minimum_fftlen = saved_minimum_fftlen;			// Restore this option to its original setting
		gwdata->required_pass2_size = 0;				// Allow any pass 2 size
		if (safe_N != N) free (safe_N);
		return (gwsetup_general_Barrett_mod_giant (gwdata, N, d));
	}

	// Allocate and init the two internal gwdatas (cyclic and negacyclic).  We must do this even if information_only is set or fft_description won't work.
	gwdata->cyclic_gwdata = cyclic_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
	gwdata->negacyclic_gwdata = negacyclic_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
	if (cyclic_gwdata == NULL || negacyclic_gwdata == NULL) {
		gwdone (gwdata);
		return (GWERROR_MALLOC);
	}
	memcpy (cyclic_gwdata, gwdata, sizeof (gwhandle));
	memcpy (negacyclic_gwdata, gwdata, sizeof (gwhandle));
	cyclic_gwdata->GW_MODULUS = negacyclic_gwdata->GW_MODULUS = NULL;
	cyclic_gwdata->parent_gwdata = negacyclic_gwdata->parent_gwdata = gwdata;

	// Now setup the cyclic and negacyclic FFT assembly code
	error_code = internal_gwsetup (cyclic_gwdata, 1.0, 2, n, -1);
	if (error_code) return (error_code);
	error_code = internal_gwsetup (negacyclic_gwdata, 1.0, 2, n, 1);
	if (error_code) return (error_code);
	ASSERTG (cyclic_gwdata->FFTLEN == fftlen && negacyclic_gwdata->FFTLEN == fftlen);
	ASSERTG (fftlen == 32 || addr_offset (cyclic_gwdata, fftlen/2-17) == addr_offset (negacyclic_gwdata, fftlen/2-17));
	ASSERTG (addr_offset (cyclic_gwdata, fftlen/2+13) == addr_offset (negacyclic_gwdata, fftlen/2+13));
	ASSERTG (cyclic_gwdata->PASS2_SIZE == negacyclic_gwdata->PASS2_SIZE);
	ASSERTG (cyclic_gwdata->num_threads == negacyclic_gwdata->num_threads);

/* Restore saved settings */

	gwdata->larger_fftlen_count = saved_larger_fftlen_count;	// though not necessary, restore this option to its original setting
	gwdata->minimum_fftlen = saved_minimum_fftlen;			// though not necessary, restore this option to its original setting

/* Init more gwdata items.  Though nearly all work is done by sub-gwdatas various informational and memory allocation routines want these items set. */

// BUG - setting the bit_length to the modulus size may break gwtogiant or ECM or any code calling popg.
// we need a better/more-consistent way of dealing with the various needed bit_lengths.  Also, PFGW should not be reading the bit_length value in integer.cpp.
//	gwdata->bit_length = bits;

	gwdata->k = 1.0;
	gwdata->b = 2;
	gwdata->n = n;
	gwdata->c = 1;
	gwdata->bit_length = n;
	gwdata->GENERAL_MMGW_MOD = TRUE;		// Set flag indicating general-purpose MMGW modulo operations are in force
	gwdata->GENERAL_MOD = FALSE;
	gwdata->FFTLEN = cyclic_gwdata->FFTLEN;
	gwdata->RATIONAL_FFT = TRUE; ASSERTG (cyclic_gwdata->RATIONAL_FFT && negacyclic_gwdata->RATIONAL_FFT);
	gwdata->avg_num_b_per_word = cyclic_gwdata->avg_num_b_per_word;
	gwdata->NUM_B_PER_SMALL_WORD = cyclic_gwdata->NUM_B_PER_SMALL_WORD;
	gwdata->fft_max_bits_per_word = negacyclic_gwdata->fft_max_bits_per_word;
	gwdata->EXTRA_BITS = cyclic_gwdata->EXTRA_BITS < negacyclic_gwdata->EXTRA_BITS ? cyclic_gwdata->EXTRA_BITS : negacyclic_gwdata->EXTRA_BITS;
	cyclic_gwdata->EXTRA_BITS = negacyclic_gwdata->EXTRA_BITS = gwdata->EXTRA_BITS;	// Use common EXTRA_BITS since cyclic results are sent to negacyclic FFTs
	gwdata->datasize = round_up_to_multiple_of (cyclic_gwdata->datasize + GW_HEADER_SIZE(negacyclic_gwdata), 128) + negacyclic_gwdata->datasize;
	gwdata->num_threads = intmin (cyclic_gwdata->num_threads, negacyclic_gwdata->num_threads);

/* Pre-compute constants needed for Montgomery-McLaughlin-Gallot-Woltman style multiplication.  In Yves' notation, R=2^n-1 and Q=2^n+1. */
/* For details see https://www.mersenneforum.org/showthread.php?p=636944 */

	if (!gwdata->information_only) {
		gwdata->FFT1_state = 0;				// FFT(1) will be needed for FMA
		atomic_set (gwdata->clone_count, 0);
		gwdata->gwnum_alloc = (gwnum *) malloc (50 * sizeof (gwnum));
		if (gwdata->gwnum_alloc == NULL) { gwdone (gwdata); return (GWERROR_MALLOC); }
		gwdata->gwnum_free = (gwnum *) malloc (50 * sizeof (gwnum));
		if (gwdata->gwnum_free == NULL) { gwdone (gwdata); return (GWERROR_MALLOC); }
		gwdata->gwnum_alloc_count = 0;
		gwdata->gwnum_alloc_array_size = 50;
		gwdata->gwnum_free_count = 0;
		gwmutex_init (&gwdata->alloc_lock);
		gwdata->GW_ALIGNMENT = cyclic_gwdata->GW_ALIGNMENT;
		gwdata->GW_ALIGNMENT_MOD = cyclic_gwdata->GW_ALIGNMENT_MOD;
		gwdata->gdata.blksize = gwnum_datasize (gwdata);

		// Allocate memory for constants
		gwdata->Np_R = gwalloc_internal (cyclic_gwdata);
		gwdata->N_Q = gwalloc_internal (negacyclic_gwdata);
		gwdata->R2_4 = gwalloc_internal (gwdata);
		if (gwdata->Np_R == NULL || gwdata->N_Q == NULL || gwdata->R2_4 == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}

		// Init auxiliary threads
		multithread_init (gwdata);

		// Finish the inverse of N mod R initialization, FFTed for future cyclic multiplications.  Yves calls this Np_R.
		gianttogw (cyclic_gwdata, Np, gwdata->Np_R);
		gwfft (cyclic_gwdata, gwdata->Np_R, gwdata->Np_R);

		// Need the modulus FFTed for future negacyclic multiplications.  Yves calls this value N_Q.
		gianttogw (negacyclic_gwdata, safe_N, gwdata->N_Q);
		if (1) {						// Sanity check 1/N.  Giants invg has been known to fail.
			gwnum tmp = gwdata->R2_4;
			gwmul3_carefully (cyclic_gwdata, gwdata->N_Q, gwdata->Np_R, tmp, GWMUL_PRESERVE_S1);
			gwtogiant (cyclic_gwdata, tmp, Np);
			if (!isone (Np)) { gwdone (gwdata); return (GWERROR_INTERNAL+13); }
			gwfree_internal_memory (cyclic_gwdata);
		}
		gwfft (negacyclic_gwdata, gwdata->N_Q, gwdata->N_Q);

		// Need R^2/4 mod N.  This will be used for a fast implementation of gianttogw.
		if (R->n[0] & 1) addg (safe_N, R);				// Make R even
		gshiftright (1, R);						// R/2
		modg (safe_N, R);						// R/2 mod N
		squareg (R);							// R^2/4
		modg (safe_N, R);						// R^2/4 mod N
		gianttogw (gwdata, R, gwdata->R2_4);
		gwfft (gwdata, gwdata->R2_4, gwdata->R2_4);
	}

	free (Np);
	free (R);
	if (safe_N != N) free (safe_N);

/* Create dummy string representation. Calling gtoc to get the first several digits would be better, but it is too slow. */

	sprintf (gwdata->GWSTRING_REP, "A %ld-bit number", bits);

/* Return success */

	return (0);
}

/* This setup routine is for operations without a modulo. In essence, */
/* you are using gwnums as a general-purpose FFT multiply library. */

int gwsetup_without_mod (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	unsigned long n)	/* Maximum number of bits in OUTPUT numbers. */
{
	const struct gwasm_jmptab *info;
	unsigned long fftlen, max_exponent, desired_n;
	int	error_code;

/* Make sure gwinit was called */

	if (gwdata->mem_needed != GWINIT_WAS_CALLED_VALUE) return (GWERROR_NO_INIT);

/* Return delayed errors from gwinit2 */

	if (gwdata->GWERROR) return (gwdata->GWERROR);

/* Call gwinfo and have it figure out the FFT length we will use.  Since the user must zero the upper half of FFT input data, the FFT */
/* outputs will be smaller.  This lets us get about another 0.15 bits per input word (data from Pavel Atnashev's primorial search). */

	gwdata->safety_margin -= 0.15f;
	error_code = gwinfo (gwdata, 1.0, 2, n, -1);
	gwdata->safety_margin += 0.15f;
	info = gwdata->jmptab;
	if (error_code) return (error_code);

	max_exponent = adjusted_max_exponent (gwdata, info);
	fftlen = info->fftlen;

/* If possible, increase n to the next multiple of FFT length.  This is because rational FFTs are faster than irrational FFTs (no FFT weights). */

	desired_n = round_up_to_multiple_of (n, fftlen);
	if (desired_n < max_exponent) n = desired_n;

/* Our FFTs don't handle cases where there are few bits per word because carries must be propagated over too many words. */
/* Arbitrarily insist that n is at least 12 * fftlen. */

	if (n < 12 * fftlen) n = 12 * fftlen;

/* Now setup the assembly code */

	gwdata->safety_margin -= 0.15f;
	error_code = internal_gwsetup (gwdata, 1.0, 2, n, -1);
	gwdata->safety_margin += 0.15f;
	if (error_code) return (error_code);

/* Set flag indicating general-purpose modulo operations are not in force */

	gwdata->GENERAL_MOD = FALSE;
	gwdata->GENERAL_MMGW_MOD = FALSE;

/* Create dummy string representation. */

	strcpy (gwdata->GWSTRING_REP, "No modulus");

/* Return success */

	return (0);
}

/* The carries area needs to be initialized by several different routines */

int asm_data_carries_size (		/* Return size (in doubles) of the carry table in an asm_data */
	gwhandle *gwdata)
{
	if (gwdata->cpu_flags & CPU_AVX512F) return (gwdata->PASS1_SIZE == 0 ? gwdata->FFTLEN / 8 : gwdata->PASS1_SIZE);
	if (gwdata->cpu_flags & CPU_AVX) return (gwdata->PASS1_SIZE);
	if (gwdata->cpu_flags & CPU_SSE2) return (gwdata->PASS1_SIZE * 2);
	return (gwdata->PASS1_SIZE);
}

void init_asm_data_carries (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)	/* Initialize carries of this asm_data */
{
	int	carry_table_size = asm_data_carries_size (gwdata);
	double	*table = asm_data->carries;
	if (gwdata->cpu_flags & CPU_AVX512F) {
		for (int i = 0; i < carry_table_size; i++) table[i] = asm_data->u.zmm.ZMM_RNDVAL;
	}
	else if (gwdata->cpu_flags & CPU_AVX) {
		double	carryval = (gwdata->b == 2) ? 3.0 * 131072.0 * 131072.0 * 131072.0 : 0.0;
		for (int i = 0; i < carry_table_size; i++) table[i] = (gwdata->ZERO_PADDED_FFT && (i & 4)) ? 0.0 : carryval;
	}
	else if (gwdata->cpu_flags & CPU_SSE2) {
		double	xmm_bigval = 3.0 * 131072.0 * 131072.0 * 131072.0;
		for (int i = 0; i < carry_table_size; i++) table[i] = xmm_bigval;
	}
	else {
		for (int i = 0; i < carry_table_size; i++) table[i] = 0.0;
	}
}

/* Clone a gwhandle.  The cloned handle can be used in a limited way in another thread.  Valid operations in the cloned handle are single threaded */
/* multiplication, addition, subtraction. */

int gwclone (
	gwhandle *cloned_gwdata,	/* Empty handle to be populated */
	gwhandle *gwdata)		/* Handle to clone */
{
	int	retcode;

/* Clones of clones are allowed.  Work back to first gwdata. */

	if (gwdata->clone_of != NULL) gwdata = gwdata->clone_of;

/* Increment cloning count.  We disable gwfree_internal_memory once a cloning occurs (the clones may be pointing to the same internal memory). */

	atomic_incr (gwdata->clone_count);

/* Start by copying the handle, set flag indicating this is a cloned handle */

	gwmutex_lock (&gwclone_lock);		// Protect against cloning while parent is atomicly changing a set of values
	memcpy (cloned_gwdata, gwdata, sizeof (struct gwhandle_struct));
	gwmutex_unlock (&gwclone_lock);
	cloned_gwdata->clone_of = gwdata;

// Init giants/gwdata sharing

	init_ghandle (&cloned_gwdata->gdata);
	cloned_gwdata->gdata.allocate = &gwgiantalloc;
	cloned_gwdata->gdata.free = &gwgiantfree;
	cloned_gwdata->gdata.deallocate = &gwgiantdealloc;
	cloned_gwdata->gdata.handle = (void *) cloned_gwdata;
	cloned_gwdata->gdata.blksize = gwnum_datasize (gwdata);

/* Can't share radix FFTs and general mod FFTs */

	cloned_gwdata->to_radix_gwdata = NULL;
	cloned_gwdata->from_radix_gwdata = NULL;
	cloned_gwdata->cyclic_gwdata = NULL;
	cloned_gwdata->negacyclic_gwdata = NULL;

/* Can't share cached GW_ADDIN.  We could if GW_ADDIN was read-only, but future calls to gwsetaddin would erroneously change/delete the shared GW_ADDIN. */

	cloned_gwdata->GW_ADDIN = NULL;
	cloned_gwdata->GW_POSTADDIN = NULL;

/* Clear pointers we're about to overwrite in case an error occurs and gwdone is called */

	cloned_gwdata->asm_data = NULL;

/* Each cloned handle must have their own asm_data structure */

	if (gwdata->asm_data != NULL) {			// GENERAL_MMGW_MOD gwdata's do not have an asm_data
		void	*cloned_asm_data_alloc;
		struct gwasm_data *asm_data, *cloned_asm_data;
		cloned_asm_data_alloc = aligned_malloc (sizeof (struct gwasm_data) + NEW_STACK_SIZE, 4096);
		if (cloned_asm_data_alloc == NULL) {
			gwdone (cloned_gwdata);
			return (GWERROR_MALLOC);
		}
		cloned_gwdata->asm_data = (char *) cloned_asm_data_alloc + NEW_STACK_SIZE;

		asm_data = (struct gwasm_data *) gwdata->asm_data;
		cloned_asm_data = (struct gwasm_data *) cloned_gwdata->asm_data;
		memcpy (cloned_asm_data, asm_data, sizeof (struct gwasm_data));
		cloned_asm_data->gwdata = cloned_gwdata;
		cloned_asm_data->thread_num = 0;
		cloned_asm_data->scratch_area = NULL;
		cloned_asm_data->carries = NULL;
		if (gwdata->SCRATCH_SIZE) {
			cloned_asm_data->scratch_area = aligned_malloc (gwdata->SCRATCH_SIZE, 64);
			if (cloned_asm_data->scratch_area == NULL) {
				gwdone (cloned_gwdata);
				return (GWERROR_MALLOC);
			}
		}
		if (gwdata->PASS2_SIZE) {
			cloned_asm_data->carries = (double *) aligned_malloc (asm_data_carries_size (gwdata) * sizeof (double), 64);
			if (cloned_asm_data->carries == NULL) {
				gwdone (cloned_gwdata);
				return (GWERROR_MALLOC);
			}
			init_asm_data_carries (gwdata, cloned_asm_data);
		}
		cloned_asm_data->MAXERR = 0.0;
	}

/* Clear counters and caches.  Each handle will keep there own counts to later be merged back into the parent gwdata. */

	cloned_gwdata->GWERROR = 0;
	cloned_gwdata->fft_count = 0;
	cloned_gwdata->read_count = 0;
	cloned_gwdata->write_count = 0;
	cloned_gwdata->saved_copyz_n = 0;

/* For now turn off multi-threading and thread callback.  We could re-visit this at a later date.  Our first use of cloned handles is in P-1 stage 2 */
/* where we are piggybacking off of polymult's thread locks and affinity setting callbacks. */

	cloned_gwdata->num_threads = 1;
	cloned_gwdata->thread_callback = NULL;
	cloned_gwdata->thread_callback_data = NULL;
	cloned_gwdata->thread_ids = NULL;
	retcode = multithread_init (cloned_gwdata);
	if (retcode) {
		gwdone (cloned_gwdata);
		return (retcode);
	}

/* Clone the general mod negacyclic gwdata */

	if (gwdata->GENERAL_MMGW_MOD) {
		cloned_gwdata->cyclic_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
		cloned_gwdata->negacyclic_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
		if (cloned_gwdata->cyclic_gwdata == NULL || cloned_gwdata->negacyclic_gwdata == NULL) {
			gwdone (cloned_gwdata);
			return (GWERROR_MALLOC);
		}
		retcode = gwclone (cloned_gwdata->cyclic_gwdata, gwdata->cyclic_gwdata);
		if (retcode) {
			gwdone (cloned_gwdata);
			return (retcode);
		}
		retcode = gwclone (cloned_gwdata->negacyclic_gwdata, gwdata->negacyclic_gwdata);
		if (retcode) {
			gwdone (cloned_gwdata);
			return (retcode);
		}
		cloned_gwdata->cyclic_gwdata->parent_gwdata = cloned_gwdata;
		cloned_gwdata->negacyclic_gwdata->parent_gwdata = cloned_gwdata;
	}

/* Return success */

	return (0);
}

/* Merge various stats (MAXERR, fft_count, etc.) back into the parent gwdata.  This routine does not do any locking to make sure the */
/* parent gwdata is not busy nor are any other cloned gwdatas simultaneously merging stats.  Locking is the caller's responsibility. */
void gwclone_merge_stats (
	gwhandle *dest_gwdata,		/* Handle to a (possibly cloned) gwdata to merge stats into */
	gwhandle *cloned_gwdata)	/* Handle to a cloned gwdata to merge stats from */
{
	struct gwasm_data *cloned_asm_data = (struct gwasm_data *) cloned_gwdata->asm_data;
	struct gwasm_data *dest_asm_data = (struct gwasm_data *) dest_gwdata->asm_data;

	dest_gwdata->GWERROR = intmax (dest_gwdata->GWERROR, cloned_gwdata->GWERROR), cloned_gwdata->GWERROR = 0;
	dest_gwdata->fft_count += cloned_gwdata->fft_count, cloned_gwdata->fft_count = 0;
	dest_gwdata->read_count += cloned_gwdata->read_count, cloned_gwdata->read_count = 0;
	dest_gwdata->write_count += cloned_gwdata->write_count, cloned_gwdata->write_count = 0;
	if (dest_asm_data != NULL) dest_asm_data->MAXERR = fltmax (dest_asm_data->MAXERR, cloned_asm_data->MAXERR), cloned_asm_data->MAXERR = 0.0;
}

/* Examine a giant to see if it a (k*2^n+c)/d value. */
/* Returns TRUE if conversion was successful. */

int convert_giant_to_k2ncd (
	giant	g,		/* Giant to examine */
	double	*k,		/* K in (K*2^N+C)/D. */
	unsigned long *n,	/* N in (K*2^N+C)/D. */
	signed long *c,		/* C in (K*2^N+C)/D. */
	unsigned long *d)	/* D in (K*2^N+C)/D. */
{
	unsigned long less_nasty_d;
	int	i;
	uint32_t quick_test;
	giant	test_g, alloc_g;
	stackgiant(tmp,3);

/* Loop through a lot of small d values in hopes of finding a multiplier that */
/* will convert the input value into a k*2^n+c value.  We do this because */
/* small d values are the ones that generate repeating bit patterns in */
/* the modulus and unexpectedly large round off errors during operations */

	alloc_g = NULL;
	less_nasty_d = 1;
	for (*d = 1; *d <= 999; *d += 2) {

/* Do a quick test to see if this is a viable candidate */

		quick_test = (g->n[1] * *d) & 0xFFFFFC00;
		if (quick_test != 0 && quick_test != 0xFFFFFC00) continue;

/* Compute g * d to see if it has the proper k*2^n+c bit pattern */

		if (*d == 1) {
			test_g = g;
		} else {
			if (alloc_g == NULL) {
				alloc_g = allocgiant (((bitlen (g) + 10) >> 5) + 1);
				if (alloc_g == NULL) return (FALSE);
			}
			ultog (*d, alloc_g);
			mulg (g, alloc_g);
			test_g = alloc_g;
		}

/* See if this d value might result in a less nasty bit pattern for */
/* emulate_mod.  For example, Phi(82730,2) behaves much better if you */
/* multiply the modulus by 11.  We'll assume that d produces a better */
/* modulus candidate if more than a quarter of the words are zero or */
/* minus one -- at least until someone improves on this scheme. */

		if (*d >= 3 && less_nasty_d == 1) {
			int	count = 0;
			for (i = 0; i < test_g->sign; i++)
				if (test_g->n[i] == 0 || test_g->n[i] == -1) count++;
			if (count >= (test_g->sign >> 2)) less_nasty_d = *d;
		}

/* See if low order 2 words are viable for a k*2^n+c candidate */

		*c = (int32_t) test_g->n[0];

		if (test_g->n[1] == 0 && test_g->n[0] <= MAX_ZEROPAD_C);
		else if (test_g->n[1] == 0xFFFFFFFF && *c < 0 && *c >= -MAX_ZEROPAD_C);
		else continue;

/* Examine the middle words */
	
		for (i = 2; i < test_g->sign - 1; i++)
			if (test_g->n[i] != test_g->n[1]) break;

/* Now see if the high bits can fit in a 51-bit k */

		tmp->n[0] = test_g->n[i]; tmp->sign = 1;
		if (test_g->sign - i >= 2) { tmp->n[1] = test_g->n[i+1]; tmp->sign = 2; }
		if (test_g->sign - i >= 3) { tmp->n[2] = test_g->n[i+2]; tmp->sign = 3; }
		if (test_g->sign - i >= 4) continue;
		if (test_g->n[1] == 0xFFFFFFFF) iaddg (1, tmp);

		*n = i * 32;
		while ((tmp->n[0] & 0x1) == 0) {
			gshiftright(1, tmp);
			(*n)++;
		}
		if (bitlen (tmp) > 51) continue;

/* Set k and return success */

		*k = tmp->n[0];
		if (tmp->sign == 2) *k += (double) tmp->n[1] * 4294967296.0;
		free (alloc_g);
		return (TRUE);
	}

/* No luck in finding a (k*2^n+c)/d equivalent for the input value */

	*d = less_nasty_d;
	free (alloc_g);
	return (FALSE);
}

/* Common setup routine for the three different user-visible setup routines */
/* Allocate memory and initialize assembly code for arithmetic modulo k*b^n+c */

int internal_gwsetup (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	const struct gwasm_jmptab *info;
	void	*asm_data_alloc;
	struct gwasm_data *asm_data;
	int	error_code;
	unsigned long mem_needed;
	double	*tables;		/* Pointer tables we are building */
	unsigned long pass1_size;
	double	small_word, big_word, temp, asm_values[50];

/* Sanity check some setup parameters */

	if (gwdata->num_threads == 0) return (GWERROR_ZERO_THREADS);

/* Remember the arguments */

	gwdata->k = k;
	gwdata->b = b;
	gwdata->n = n;
	gwdata->c = c;

/* Init the FPU to assure we are in 64-bit precision mode */

	fpu_init ();

/* Initialize the clone mutex if necessary */

	if (!gwclone_lock_initialized) {
		gwmutex_init (&gwclone_lock);
		gwclone_lock_initialized = TRUE;
	}

/* Allocate space for the assembly code global data.  This area is preceded */
/* by a temporary stack.  This allows the assembly code to access the global */
/* data using offsets from the stack pointer. */

	asm_data_alloc = aligned_malloc (sizeof (struct gwasm_data) + NEW_STACK_SIZE, 4096);
	if (asm_data_alloc == NULL) {
		gwdone (gwdata);
		return (GWERROR_MALLOC);
	}
	gwdata->asm_data = (char *) asm_data_alloc + NEW_STACK_SIZE;
	asm_data = (struct gwasm_data *) gwdata->asm_data;
	memset (asm_data, 0, sizeof (struct gwasm_data));

/* Select the proper FFT size for this k,b,n,c combination */

	error_code = gwinfo (gwdata, k, b, n, c);
	if (error_code) {
		gwdone (gwdata);
		return (error_code);
	}
	info = gwdata->jmptab;

/* Get pointer to fft info and allocate needed memory.  If we are trying to allocate large pages, then also allocate space for */
/* one gwnum value to reduce wasted space ever so slightly (1MB on average).  We allocate a little extra space to align the gwnum */
/* on a cache line boundary and because gwnum_size may not produce an accurate value prior to gwsetup completing. */

	if (!gwdata->information_only) {
		tables = NULL;
		gwdata->large_pages_ptr = NULL;
		gwdata->large_pages_gwnum = NULL;
		mem_needed = gwdata->mem_needed + gwdata->SCRATCH_SIZE;
		if (gwdata->use_large_pages) {
			tables = (double *) large_pages_malloc (mem_needed + gwnum_size (gwdata) + 4096);
			if (tables != NULL) {
				/* Save large pages pointer for later freeing */
				gwdata->large_pages_ptr = tables;
				tables = align_ptr (tables, 128);
				/* Save pointer to the gwnum we also allocated, so that first gwalloc call can return it. */
				gwdata->large_pages_gwnum = align_ptr ((char *) tables + mem_needed, 128);
			}
		}
		if (tables == NULL) {
			tables = (double *) aligned_malloc (mem_needed, 4096);
			if (tables == NULL) return (GWERROR_MALLOC);
		}
		gwdata->gwnum_memory = tables;

/* Do a seemingly pointless memset! */
/* The memset will walk through the allocated memory sequentially, which */
/* increases the likelihood that contiguous virtual memory will map to */
/* contiguous physical memory. */

		memset (tables, 0, mem_needed);
	}

/* Copy values for asm code to use */

	asm_data->FFTLEN = gwdata->FFTLEN;
	asm_data->ZERO_PADDED_FFT = gwdata->ZERO_PADDED_FFT;
	asm_data->NEGACYCLIC_FFT = gwdata->NEGACYCLIC_FFT;
	asm_data->B_IS_2 = (b == 2);

/* Initialize the extended precision code that computes the FFT weights */

	if (!gwdata->information_only) {
		gwdata->dd_data = gwdbldbl_data_alloc ();
		if (gwdata->dd_data == NULL) {
			gwdone (gwdata);
			return (GWERROR_MALLOC);
		}
		gwfft_weight_setup (gwdata->dd_data, gwdata->ZERO_PADDED_FFT, k, b, n, c, gwdata->FFTLEN);
	}

/* Calculate the number of bits in k*b^n.  This will be helpful in */
/* determining how much memory to allocate for giants. */

	gwdata->bit_length = log2 (k) + n * log2 (b);

/* Calculate the average number of base b's stored in each FFT word.  The total */
/* number of base b's the underlying FFT works with (i.e. the point at which data */
/* wraps around to the low FFT word) is 2*n for a zero pad FFT and logb(k) + n otherwise. */

	gwdata->avg_num_b_per_word = (gwdata->ZERO_PADDED_FFT ? n * 2.0 : (log (k) / log (b) + n)) / gwdata->FFTLEN;

/* Calculate the number of base b's stored in each small FFT word. */

	gwdata->NUM_B_PER_SMALL_WORD = (unsigned long) gwdata->avg_num_b_per_word;
	small_word = pow ((double) b, gwdata->NUM_B_PER_SMALL_WORD);
	big_word = (double) b * small_word;

/* Set a flag if this is a rational FFT.  That is, an FFT where all the weighting factors are 1.0. */
/* This happens when zero-padded or abs(c) is 1, and every FFT word has the same number of b's. */
/* The assembly code can make some obvious optimizations when all the FFT weights are one. */

	gwdata->RATIONAL_FFT = asm_data->RATIONAL_FFT =
		((double) gwdata->NUM_B_PER_SMALL_WORD == gwdata->avg_num_b_per_word) && (gwdata->ZERO_PADDED_FFT || labs (c) == 1);

/* Remember the maximum number of bits per word that this FFT length supports.  We use this in gwnear_fft_limit. */
/* Note that zero padded FFTs can support an extra 0.3 bits per word because of the all the zeroes. */

	gwdata->fft_max_bits_per_word = (double) adjusted_max_exponent (gwdata, info) / (double) gwdata->FFTLEN;
	if (gwdata->ZERO_PADDED_FFT) gwdata->fft_max_bits_per_word += 0.3;

/* Compute extra bits in FFT output words.  The maximum exponent for an FFT size is computed based on gwsquare operations. */
/* Gwmul operations generate less roundoff error, giving us approximately 0.527 more bits in the FFT output words. */
/* Coincedentally, an unnormalized add as input to gwmul will cost about 0.509 more bits in the FFT output words.  This led to the */
/* advice that one unnormalized add can be an input to a multiply operation.  EXTRA_BITS allow us to take this a step further. */
/* When testing exponents below the FFT limit, we may be able to do even more unnormalized adds prior to a gwsquare or gwmul operation. */
/* We'll automatically convert some gwadd and gwsub operations into unnormalized gwaddquick and gwsubquick operations. */
/* EXTRA_BITS measures the number of extra bits available in FFT output words for a gwmul operation.  NOTE: Under normal circumstances, */
/* max_bits will be greater than virtual bits, but playing with the safety margin or forcing use of a specific FFT length could change that. */

	gwdata->EXTRA_BITS = (float) ((gwdata->fft_max_bits_per_word - gwdata->safety_margin - gwdata->polymult_safety_margin - virtual_bits_per_word (gwdata)) * 2.0);
	if (gwdata->EXTRA_BITS < 0.0f) gwdata->EXTRA_BITS = 0.0f;
	gwdata->EXTRA_BITS += EB_GWMUL_SAVINGS;

/* Determine the pass 1 size.  This affects how we build many of the sin/cos tables. */

	if (gwdata->PASS2_SIZE)
		pass1_size = gwdata->PASS1_SIZE;
	else
		pass1_size = gwdata->FFTLEN;

/* Initialize weights, sin/cos tables, constants based on CPU architecture.  Not needed for information only gwsetup. */

	if (!gwdata->information_only) {

/* Initialize tables for r4dwpn AVX-512 FFT code */

	    if (gwdata->cpu_flags & CPU_AVX512F) {
		double	rndval_base;
		int	cnt;

/* Compute the normalization constants.  The rounding constant, a value larger than 3*2^51, is chosen such that */
/* it is a multiple of big_word and RNDVAL * big_word - RNDVAL fits in 53 bits. */
/* Let's figure out how to compute RNDVAL, by imagining a CPU with 20 bit mantissas instead of 53-bits */
/* First example is bigbase = 2^8, RNDVAL is 3*2^18: */
/*	RNDVAL = 1100 0000 0000 0000 0000 */
/*	RNDVAL * bigbase - RNDVAL = 1100 0000 0000 0000 0000 0000 0000 */
/*					    - 1100 0000 0000 0000 0000, which easily fits in 20 bits. */
/* Next example is a with 6-bit odd bigbase.  We choose RNDVAL such that it is a multiple of bigbase and a multiple of 2^6. */
/*	RNDVAL = 1100 0000 xxxx xx00 0000 */
/*	RNDVAL * bigbase - RNDVAL = yy yyyy yyyy yyyy yyyy yy00 0000 */
/*					  - 1100 0000 xxxx xx00 0000, which fits in 20 bits. */
/* Next example is a with 6-bit even bigbase.  We choose RNDVAL such that it is a multiple of bigbase and a multiple of 2^5. */
/*	RNDVAL = 1100 0000 0xxx xxx0 0000 */
/*	RNDVAL * bigbase - RNDVAL = yy yyyy yyyy yyyy yyyy yy00 0000 */
/*					  - 1100 0000 0xxx xxx0 0000, */
/* which does NOT fit in 20 bits, we must choose RNDVAL as a multiple of 2^6. */
/* This leaves us with the following simple algorithm: */
/*	cnt = count of bits in bigbase */
/*	RNDVAL = 3*2^51 / 2^cnt */
/*	RNDVAL += (bigbase - RNDVAL % bigbase) % bigbase */
/*	RNDVAL *= 2^cnt */

		asm_data->u.zmm.ZMM_LARGE_BASE = big_word;				/* Upper limit */
		asm_data->u.zmm.ZMM_LARGE_BASE_INVERSE = 1.0 / big_word;		/* Upper limit inverse */
		asm_data->u.zmm.ZMM_SMALL_BASE = small_word;				/* Lower limit */
		asm_data->u.zmm.ZMM_SMALL_BASE_INVERSE = 1.0 / small_word;		/* Lower limit inverse */

		// Use small_word for FFTs where there are no big words (and big_word might exceed fatal 2^26)
		// This is more than just RATIONAL_FFTs (e.g. 117^3072-5).
		rndval_base = ((double) gwdata->NUM_B_PER_SMALL_WORD == gwdata->avg_num_b_per_word ? small_word : big_word);
		ASSERTG (rndval_base < 67108864.0);
		cnt = (int) log2 (rndval_base) + 1;
		asm_data->u.zmm.ZMM_RNDVAL = 3.0 * 131072.0 * 131072.0 * 131072.0;	/* Rounding value */
		asm_data->u.zmm.ZMM_RNDVAL /= pow (2.0, cnt);
		asm_data->u.zmm.ZMM_RNDVAL += fltmod (rndval_base - fltmod (asm_data->u.zmm.ZMM_RNDVAL, rndval_base), rndval_base);
		asm_data->u.zmm.ZMM_RNDVAL *= pow (2.0, cnt);

		asm_data->u.zmm.ZMM_RNDVAL_TIMES_LARGE_BASE = asm_data->u.zmm.ZMM_RNDVAL * big_word - asm_data->u.zmm.ZMM_RNDVAL;
		asm_data->u.zmm.ZMM_RNDVAL_TIMES_SMALL_BASE = asm_data->u.zmm.ZMM_RNDVAL * small_word - asm_data->u.zmm.ZMM_RNDVAL;
		asm_data->u.zmm.ZMM_RNDVAL_OVER_LARGE_BASE = asm_data->u.zmm.ZMM_RNDVAL / big_word - asm_data->u.zmm.ZMM_RNDVAL;
		asm_data->u.zmm.ZMM_RNDVAL_OVER_SMALL_BASE = asm_data->u.zmm.ZMM_RNDVAL / small_word - asm_data->u.zmm.ZMM_RNDVAL;

/* Build sin/cos and premultiplier tables used in pass 1 of two pass FFTs. */
/* For best prefetching, make sure tables remain on 64-byte boundaries */

		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		gwdata->pass1_var_data = tables;
		tables = zr4dwpn_build_pass1_table (gwdata, tables);
		tables = round_to_cache_line (tables);
		/* The wrapper for "one-pass" FFTs does not use a fixed sin/cos table, but it does access the variable data using sincos2 */
		if (gwdata->PASS1_SIZE == 0) {
			asm_data->sincos2 = gwdata->pass1_var_data;
		} else {
			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->sincos2 = tables;
			tables = zr4dwpn_build_fixed_pass1_table (gwdata, tables);
			asm_data->sincos2 = share_sincos_data (gwdata, FIXED_PASS1_SINCOS_DATA, asm_data->sincos2, (char *) tables - (char *) asm_data->sincos2);
			tables = round_to_cache_line (tables);
		}

/* Build the sin/cos table used in complex pass 2 blocks */
/* The pass 2 tables are the same as for a traditional radix-4 FFT */		

		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		asm_data->xsincos_complex = tables;
		tables = zr4_build_pass2_complex_table (gwdata, tables);
		asm_data->xsincos_complex = share_sincos_data (gwdata, PASS2_COMPLEX_SINCOS_DATA, asm_data->xsincos_complex, (char *) tables - (char *) asm_data->xsincos_complex);
		tables = round_to_cache_line (tables);
		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		asm_data->sincos3 = tables;
		tables = zr4_build_pass2_real_table (gwdata, tables);
		asm_data->sincos3 = share_sincos_data (gwdata, PASS2_REAL_SINCOS_DATA, asm_data->sincos3, (char *) tables - (char *) asm_data->sincos3);
		tables = round_to_cache_line (tables);

/* Allocate a table for carries.  For better distribution of data in the caches, make this table contiguous with all */
/* other data used in the first pass (scratch area, normalization tables, etc.)  Note that we put the tables that */
/* are only partly loaded (column multipliers and big/lit table) after the tables that are loaded throughout the first pass. */

		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		asm_data->carries = tables;
		if (gwdata->PASS1_SIZE == 0) tables += gwdata->FFTLEN / 8;	// Every 8th one-pass FFT element needs a carry
		else tables += gwdata->PASS1_SIZE;				// Pass 1 size - each needs a carry
		tables = round_to_cache_line (tables);

/* Build the group muliplier normalization table.  Keep this table contiguous with other data used in pass 1. */

		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		asm_data->norm_grp_mults = tables;
		tables = zr4dwpn_build_norm_table (gwdata, tables);
		tables = round_to_cache_line (tables);

/* Reserve room for the pass 1 scratch area. */

		if (gwdata->SCRATCH_SIZE) {
			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->scratch_area = tables;
			tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			tables = round_to_cache_line (tables);
		}

/* Build the table of big vs. little flags.  Build the table of fudge factor flags */

		ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
		tables = zr4dwpn_build_biglit_table (gwdata, tables);
		tables = round_to_cache_line (tables);
		tables = zr4dwpn_build_fudge_table (gwdata, tables);
		tables = round_to_cache_line (tables);

#ifdef GDEBUG_MEM
		{
		char buf[80];
		sprintf (buf, "FFTlen: %d, clm: %d\n", (int) gwdata->FFTLEN, (int) gwdata->PASS1_CACHE_LINES); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, scratch area: %d\n", (int) gwdata->FFTLEN, (int) gwdata->SCRATCH_SIZE); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass1 var s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->sincos2 - (intptr_t) gwdata->pass1_var_data)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass1 fixed s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->xsincos_complex - (intptr_t) asm_data->sincos2)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass2 complex s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->sincos3 - (intptr_t) asm_data->xsincos_complex)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass2 real s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->carries - (intptr_t) asm_data->sincos3)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, carries: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->norm_grp_mults - (intptr_t) asm_data->carries)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, norm grp mults: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->scratch_area - (intptr_t) asm_data->norm_grp_mults)); OutputBoth (0, buf);
		}
#endif

/* Create offsets for carry propagation code to step through eight FFT data elements */

		asm_data->u.zmm.ZMM_SRC_INCR = (intptr_t) addr_offset (gwdata, 1) - (intptr_t) addr_offset (gwdata, 0);
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 2) - (intptr_t) addr_offset (gwdata, 1));
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 3) - (intptr_t) addr_offset (gwdata, 2));
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 4) - (intptr_t) addr_offset (gwdata, 3));
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 5) - (intptr_t) addr_offset (gwdata, 4));
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 6) - (intptr_t) addr_offset (gwdata, 5));
		ASSERTG (asm_data->u.zmm.ZMM_SRC_INCR == (intptr_t) addr_offset (gwdata, 7) - (intptr_t) addr_offset (gwdata, 6));
	    }

/* Initialize tables for AVX FFT code */

	    else if (gwdata->cpu_flags & CPU_AVX) {

/* Initialize tables for the one pass radix-4 FFT assembly code. */

		if (gwdata->PASS2_SIZE == 0) {

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->sincos1 = tables;
			tables = yr4_build_onepass_sincos_table (gwdata, tables);

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->norm_col_mults = tables;
			tables = yr4_build_onepass_norm_table (gwdata, tables);

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->norm_biglit_array = tables;
			tables = yr4_build_onepass_biglit_table (gwdata, tables);
		}

/* Initialize tables for an r4delay FFT with partial normalization (r4dwpn). */

		else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {

/* Build sin/cos and premultiplier tables used in pass 1 of two pass FFTs. */
/* For best prefetching, make sure tables remain on 64-byte boundaries */

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			tables = yr4dwpn_build_pass1_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->sincos2 = tables;
			tables = yr4dwpn_build_fixed_pass1_table (gwdata, tables);
			asm_data->sincos2 = share_sincos_data (gwdata, FIXED_PASS1_SINCOS_DATA, asm_data->sincos2, (char *) tables - (char *) asm_data->sincos2);

/* Build the sin/cos table used in complex pass 2 blocks */
/* The pass 2 tables are the same as for a traditional radix-4 FFT */		

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->xsincos_complex = tables;
			tables = yr4_build_pass2_complex_table (gwdata, tables);
			asm_data->xsincos_complex = share_sincos_data (gwdata, PASS2_COMPLEX_SINCOS_DATA, asm_data->xsincos_complex, (char *) tables - (char *) asm_data->xsincos_complex);
			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->sincos3 = tables;
			tables = yr4_build_pass2_real_table (gwdata, tables);
			asm_data->sincos3 = share_sincos_data (gwdata, PASS2_REAL_SINCOS_DATA, asm_data->sincos3, (char *) tables - (char *) asm_data->sincos3);

/* Allocate a table for carries.  Init with YMM_BIGVAL.  For better distribution of data */
/* in the L2 cache, make this table contiguous with all other data used in the first pass */
/* (scratch area, normalization tables, etc.)  Note that we put the tables that are only partly loaded */
/* (column multipliers and big/lit table) after the tables that are loaded throughout the first pass. */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
				asm_data->carries = tables;
				tables += gwdata->PASS1_SIZE;
				tables += (8 - (tables - gwdata->gwnum_memory)) & 7;
			}

/* Build the group muliplier normalization table.  Keep this table contiguous with other data used in pass 1. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->norm_grp_mults = tables;
			tables = yr4dwpn_build_norm_table (gwdata, tables);

/* Reserve room for the pass 1 scratch area. */

			if (gwdata->SCRATCH_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
				asm_data->scratch_area = tables;
				tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			}

/* Build the table of big vs. little flags. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 7) == 0);
			asm_data->norm_biglit_array = tables;
			tables = yr4dwpn_build_biglit_table (gwdata, tables);
			tables += (8 - (tables - gwdata->gwnum_memory)) & 7;

/* Build the column normalization multiplier table. */

			asm_data->norm_col_mults = NULL;
		}

#ifdef GDEBUG_MEM
	if (gwdata->PASS2_SIZE) {
		char buf[80];
		sprintf (buf, "FFTlen: %d, clm: %d, count3: %d, count2: %d\n", (int) gwdata->FFTLEN, (int) gwdata->PASS1_CACHE_LINES, (int) asm_data->count3, (int) asm_data->count2); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, scratch area: %d\n", (int) gwdata->FFTLEN, (int) gwdata->SCRATCH_SIZE); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass1 var s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->sincos2 - (intptr_t) gwdata->pass1_var_data)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass1 fixed s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->xsincos_complex - (intptr_t) asm_data->sincos2)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass2 complex s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->sincos3 - (intptr_t) asm_data->xsincos_complex)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, pass2 real s/c data: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->carries - (intptr_t) asm_data->sincos3)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, carries: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->norm_grp_mults - (intptr_t) asm_data->carries)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, norm grp mults: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) asm_data->scratch_area - (intptr_t) asm_data->norm_grp_mults)); OutputBoth (0, buf);
		sprintf (buf, "FFTlen: %d, norm biglit: %d\n", (int) gwdata->FFTLEN, (int) ((intptr_t) tables - (intptr_t) asm_data->norm_biglit_array)); OutputBoth (0, buf);
	}
#endif

/* Create offsets for carry propagation code to step through norm array */

		asm_data->u.ymm.YMM_SRC_INCR7 = (intptr_t) addr_offset (gwdata, 7) - (intptr_t) addr_offset (gwdata, 6);
		asm_data->u.ymm.YMM_SRC_INCR6 = (intptr_t) addr_offset (gwdata, 6) - (intptr_t) addr_offset (gwdata, 5);
		asm_data->u.ymm.YMM_SRC_INCR5 = (intptr_t) addr_offset (gwdata, 5) - (intptr_t) addr_offset (gwdata, 4);
		asm_data->u.ymm.YMM_SRC_INCR4 = (intptr_t) addr_offset (gwdata, 4) - (intptr_t) addr_offset (gwdata, 3);
		asm_data->u.ymm.YMM_SRC_INCR3 = (intptr_t) addr_offset (gwdata, 3) - (intptr_t) addr_offset (gwdata, 2);
		asm_data->u.ymm.YMM_SRC_INCR2 = (intptr_t) addr_offset (gwdata, 2) - (intptr_t) addr_offset (gwdata, 1);
		asm_data->u.ymm.YMM_SRC_INCR1 = (intptr_t) addr_offset (gwdata, 1) - (intptr_t) addr_offset (gwdata, 0);
	    }

/* Initialize tables for SSE2 FFTs */

	    else if (gwdata->cpu_flags & CPU_SSE2) {

/* Initialize tables for the radix-4 FFT assembly code.  These are guaranteed to be */
/* two-pass FFTs as we use the older FFT code for one pass FFTs. */

		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4) {

/* Build sin/cos and premultiplier tables used in pass 1 of two pass FFTs. */
/* For best prefetching, make sure tables remain on 128-byte boundaries */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			tables = r4_build_pass1_table (gwdata, tables);

/* Build the sin/cos table used in complex pass 2 blocks */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->xsincos_complex = tables;
			tables = r4_build_pass2_complex_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos3 = tables;
			tables = r4_build_pass2_real_table (gwdata, tables);

/* Allocate a table for carries.  Init with XMM_BIGVAL.  For best distribution of data in the L2 cache, make this table contiguous */
/* with all other data used in the first pass (scratch area, normalization tables, etc.)  Note that we put the tables that are only partly loaded */
/* (column multipliers and big/lit table) after the tables that are loaded throughout the first pass. */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->carries = tables;
				tables += gwdata->PASS1_SIZE * 2;
				tables += (16 - (tables - gwdata->gwnum_memory)) & 15;
			}

/* Build the group muliplier normalization table.  Keep this table */
/* contiguous with other data used in pass 1. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_grp_mults = tables;
			tables = r4_build_norm_table (gwdata, tables, 0);

/* Reserve room for the pass 1 scratch area. */

			if (gwdata->SCRATCH_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->scratch_area = tables;
				tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			}

/* Build the table of big vs. little flags.  This cannot be last table */
/* built as xnorm_2d macro reads 2 bytes past the end of the array. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_biglit_array = tables;
			tables = r4_build_biglit_table (gwdata, tables);
			tables += (16 - (tables - gwdata->gwnum_memory)) & 15;

/* Build the column normalization multiplier table. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_col_mults = tables;
			tables = r4_build_norm_table (gwdata, tables, 1);
		}

/* Initialize tables for a modified radix-4 FFT.  This particular FFT */
/* uses a radix-8 building block when there are an odd number of FFT */
/* levels in a pass. */

		else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DELAYED) {

/* Build sin/cos and premultiplier tables used in pass 1 of two pass FFTs. */
/* For best prefetching, make sure tables remain on 128-byte boundaries */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			tables = r4delay_build_pass1_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos1 = tables;
			tables = r4delay_build_fixed_premult_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos2 = tables;
			tables = r4delay_build_fixed_pass1_table (gwdata, tables);

/* Build the sin/cos table used in complex pass 2 blocks */
/* The pass 2 tables are the same as for a traditional radix-4 FFT */		

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->xsincos_complex = tables;
			tables = r4_build_pass2_complex_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos3 = tables;
			tables = r4_build_pass2_real_table (gwdata, tables);

/* Allocate a table for carries.  Init with XMM_BIGVAL.  For best distribution of data in the L2 cache, make this table contiguous */
/* with all other data used in the first pass (scratch area, normalization tables, etc.)  Note that we put the tables that are only partly loaded */
/* (column multipliers and big/lit table after the tables that are loaded throughout the first pass. */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->carries = tables;
				tables += gwdata->PASS1_SIZE * 2;
				tables += (16 - (tables - gwdata->gwnum_memory)) & 15;
			}

/* Build the group muliplier normalization table.  Keep this table */
/* contiguous with other data used in pass 1. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_grp_mults = tables;
			tables = r4_build_norm_table (gwdata, tables, 0);

/* Reserve room for the pass 1 scratch area. */

			if (gwdata->SCRATCH_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->scratch_area = tables;
				tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			}

/* Build the table of big vs. little flags.  This cannot be last table */
/* built as xnorm_2d macro reads 2 bytes past the end of the array. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_biglit_array = tables;
			tables = r4_build_biglit_table (gwdata, tables);
			tables += (16 - (tables - gwdata->gwnum_memory)) & 15;

/* Build the column normalization multiplier table. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_col_mults = tables;
			tables = r4_build_norm_table (gwdata, tables, 1);
		}

/* Initialize tables for an r4delay FFT with partial normalization (r4dwpn). */

		else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {

/* Build sin/cos and premultiplier tables used in pass 1 of two pass FFTs. */
/* For best prefetching, make sure tables remain on 128-byte boundaries */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			tables = r4dwpn_build_pass1_table (gwdata, tables);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos1 = tables;
			tables = r4delay_build_fixed_premult_table (gwdata, tables);
			asm_data->sincos1 = share_sincos_data (gwdata, FIXED_PASS1_SINCOS_DATA, asm_data->sincos1, (char *) tables - (char *) asm_data->sincos1);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos2 = tables;
			tables = r4delay_build_fixed_pass1_table (gwdata, tables);
			asm_data->sincos2 = share_sincos_data (gwdata, FIXED_PASS1_SINCOS_DATA, asm_data->sincos2, (char *) tables - (char *) asm_data->sincos2);

/* Build the sin/cos table used in complex pass 2 blocks */
/* The pass 2 tables are the same as for a traditional radix-4 FFT */		

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->xsincos_complex = tables;
			tables = r4_build_pass2_complex_table (gwdata, tables);
			asm_data->xsincos_complex = share_sincos_data (gwdata, PASS2_COMPLEX_SINCOS_DATA, asm_data->xsincos_complex, (char *) tables - (char *) asm_data->xsincos_complex);
			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos3 = tables;
			tables = r4_build_pass2_real_table (gwdata, tables);
			asm_data->sincos3 = share_sincos_data (gwdata, PASS2_REAL_SINCOS_DATA, asm_data->sincos3, (char *) tables - (char *) asm_data->sincos3);

/* Allocate a table for carries.  Init with XMM_BIGVAL.  For best distribution of data in the L2 cache, make this table contiguous */
/* with all other data used in the first pass (scratch area, normalization tables, etc.)  Note that we put the tables that are only partly loaded */
/* (column multipliers and big/lit table after the tables that are loaded throughout the first pass. */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->carries = tables;
				tables += gwdata->PASS1_SIZE * 2;
				tables += (16 - (tables - gwdata->gwnum_memory)) & 15;
			}

/* Build the group multiplier normalization table.  Keep this table contiguous with other data used in pass 1. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_grp_mults = tables;
			tables = r4dwpn_build_norm_table (gwdata, tables);

/* Reserve room for the pass 1 scratch area. */

			if (gwdata->SCRATCH_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->scratch_area = tables;
				tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			}

/* Build the table of big vs. little flags.  This cannot be last table */
/* built as xnorm_2d macro reads 2 bytes past the end of the array. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_biglit_array = tables;
			tables = r4dwpn_build_biglit_table (gwdata, tables);
			tables += (16 - (tables - gwdata->gwnum_memory)) & 15;

/* Build the column normalization multiplier table. */

			asm_data->norm_col_mults = NULL;
		}

/* Initialize tables for the home-grown SSE2 FFT code. */

		else {

/* Build sin/cos and premultiplier tables used in pass 2 of two pass FFTs */
/* Remember that pass2_size is the number of complex values in a pass 2 */
/* section, but build_sin_cos_table wants the number of reals in a section. */
/* However, we build a 1/4-sized table by mixing some of the sin/cos */
/* data into the premultiplier table.  So, divide pass2_size by 2 instead of */
/* multiplying pass2_size by 2. */

/* For best prefetching, make sure tables remain on 128-byte boundaries */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->u.xmm.pass2_premults = tables;
				tables = hg_build_premult_table (gwdata, tables);

/* Build the rest of the tables */

				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->xsincos_complex = tables;
				tables = hg_build_sin_cos_table (tables, gwdata->PASS2_SIZE/2, 0, 1);

				if (!gwdata->NEGACYCLIC_FFT) {
					ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
					asm_data->u.xmm.sincos6 = tables;
					tables = hg_build_sin_cos_table (tables, gwdata->PASS2_SIZE * 4, 1, 2);
					asm_data->u.xmm.sincos7 = tables;
					tables = hg_build_sin_cos_table (tables, gwdata->PASS2_SIZE, 1, 1);
					tables += (16 - (tables - gwdata->gwnum_memory)) & 15;
				}

				asm_data->u.xmm.sincos8 = asm_data->u.xmm.sincos7;
				asm_data->u.xmm.sincos9 = asm_data->u.xmm.sincos7;
				asm_data->u.xmm.sincos10 = asm_data->u.xmm.sincos7;
				asm_data->u.xmm.sincos11 = asm_data->u.xmm.sincos7;
			}

/* Allocate a table for carries.  Init with XMM_BIGVAL.  For best distribution of data in the L2 cache, make this table contiguous */
/* with all other data used in the first pass (scratch area, normalization tables, etc.)  Note that we put the tables that are only partly loaded */
/* (column multipliers and big/lit table after the tables that are loaded throughout the first pass. */

			if (gwdata->PASS2_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->carries = tables;
				tables += gwdata->PASS1_SIZE * 2;
				tables += (16 - (tables - gwdata->gwnum_memory)) & 15;
			}

/* Build the group muliplier normalization table.  Keep this table */
/* contiguous with other data used in pass 1. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_grp_mults = tables;
			tables = hg_build_norm_table (gwdata, tables, 0);

/* Build the plus1-pre-multiplier table (complex weights applied when c > 0 */
/* and we are doing a negacyclic FFT rather than emulating it with a zero-padded FFT. */

			if (gwdata->NEGACYCLIC_FFT) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->plus1_premults = tables;
				tables = hg_build_plus1_table (gwdata, tables);
			}

/* Reserve room for the pass 1 scratch area. */

			if (gwdata->SCRATCH_SIZE) {
				ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
				asm_data->scratch_area = tables;
				tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);
			}

/* Build sin/cos tables used in pass 1.  If FFTLEN is a power of two, */
/* many of the sin/cos tables can be shared. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->sincos1 = tables;
			tables = hg_build_sin_cos_table (tables, pass1_size, !gwdata->NEGACYCLIC_FFT, gwdata->PASS2_SIZE == 0 ? 2 : 1);

			if (gwdata->PASS2_SIZE && pass1_size == pow_two_above_or_equal (pass1_size))
				asm_data->sincos2 = asm_data->sincos1;
			else {
				asm_data->sincos2 = tables;
				tables = hg_build_sin_cos_table (tables, pass1_size/4, !gwdata->NEGACYCLIC_FFT, 1);
			}

			if (pass1_size == pow_two_above_or_equal (pass1_size)) {
				asm_data->sincos3 = asm_data->sincos2;
				asm_data->sincos4 = asm_data->sincos2;
				asm_data->sincos5 = asm_data->sincos2;
			} else {
				asm_data->sincos3 = tables;
				tables = hg_build_sin_cos_table (tables, pass1_size/16, !gwdata->NEGACYCLIC_FFT, 1);
				asm_data->sincos4 = tables;
				tables = hg_build_sin_cos_table (tables, pass1_size/64, !gwdata->NEGACYCLIC_FFT, 1);
				asm_data->sincos5 = tables;
				tables = hg_build_sin_cos_table (tables, pass1_size/256, !gwdata->NEGACYCLIC_FFT, 1);
			}
			tables += (16 - (tables - gwdata->gwnum_memory)) & 15;

/* Build the table of big vs. little flags.  This cannot be last table */
/* built as xnorm_2d macro reads 2 bytes past the end of the array. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_biglit_array = tables;
			tables = hg_build_biglit_table (gwdata, tables);
			tables += (16 - (tables - gwdata->gwnum_memory)) & 15;

/* Build the column normalization multiplier table. */

			ASSERTG (((tables - gwdata->gwnum_memory) & 15) == 0);
			asm_data->norm_col_mults = tables;
			tables = hg_build_norm_table (gwdata, tables, 1);
		}
	    }

/* Initialze table for the x87 assembly code. */

#ifndef X86_64

	    else {

/* Allocate a table for carries.  Init with zero.  For best */
/* distribution of data in the L2 cache, make this table contiguous */
/* with the scratch area which is also used in the first pass. */

		if (gwdata->PASS2_SIZE) {
			asm_data->carries = tables;
			tables += gwdata->PASS1_SIZE;
		}

/* Reserve room for the pass 1 scratch area. */

		asm_data->scratch_area = tables;
		if (gwdata->SCRATCH_SIZE)
			tables = (double *) ((char *) tables + gwdata->SCRATCH_SIZE);

/* Build the group muliplier normalization table.  Keep this table */
/* contiguous with other data used in pass 1. */

		asm_data->norm_grp_mults = tables;
		tables = x87_build_norm_table (gwdata, tables, 0);

/* Build sin/cos tables used in pass 1.  If FFTLEN is a power of two, */
/* many of the sin/cos tables can be shared. */

		asm_data->sincos1 = tables;
		tables = x87_build_sin_cos_table (tables, pass1_size, !gwdata->NEGACYCLIC_FFT);

		if (pass1_size == pow_two_above_or_equal (pass1_size)) {
			asm_data->sincos2 = asm_data->sincos1;
			asm_data->sincos3 = asm_data->sincos1;
			asm_data->sincos4 = asm_data->sincos1;
			asm_data->sincos5 = asm_data->sincos1;
		} else {
			asm_data->sincos2 = tables;
			tables = x87_build_sin_cos_table (tables, pass1_size/4, !gwdata->NEGACYCLIC_FFT);
			asm_data->sincos3 = tables;
			tables = x87_build_sin_cos_table (tables, pass1_size/16, !gwdata->NEGACYCLIC_FFT);
			asm_data->sincos4 = tables;
			tables = x87_build_sin_cos_table (tables, pass1_size/64, !gwdata->NEGACYCLIC_FFT);
			asm_data->sincos5 = tables;
			tables = x87_build_sin_cos_table (tables, pass1_size/256, !gwdata->NEGACYCLIC_FFT);
		}

/* Build sin/cos and premultiplier tables used in pass 2 of two pass FFTs */
/* Remember that pass2_size is the number of complex values in a pass 2 */
/* section, but x87_build_sin_cos_table wants the number of reals in a section. */

		if (gwdata->PASS2_SIZE) {
			asm_data->u.x87.pass2_premults = tables;
			tables = x87_build_premult_table (gwdata, tables);
			asm_data->xsincos_complex = tables;
			tables = x87_build_sin_cos_table (tables, gwdata->PASS2_SIZE*2, 0);

			if (!gwdata->NEGACYCLIC_FFT) {
				asm_data->u.x87.sincos6 = tables;
				tables = x87_build_sin_cos_table (tables, gwdata->PASS2_SIZE*2, 1);
				asm_data->u.x87.sincos7 = asm_data->u.x87.sincos6;
				asm_data->u.x87.sincos8 = asm_data->u.x87.sincos6;
				asm_data->u.x87.sincos9 = asm_data->u.x87.sincos6;
				asm_data->u.x87.sincos10 = asm_data->u.x87.sincos6;
			}
		}

/* Build the plus1-pre-multiplier table (complex weights applied when c > 0 */
/* and we are doing a negacyclic FFT rather than emulating it with a zero-padded FFT. */

		if (gwdata->NEGACYCLIC_FFT) {
			asm_data->plus1_premults = tables;
			tables = x87_build_plus1_table (gwdata, tables);
		}

/* Build the column normalization multiplier table. */

		asm_data->norm_col_mults = tables;
		tables = x87_build_norm_table (gwdata, tables, 1);

/* Build the table of big vs. little flags. */

		asm_data->norm_biglit_array = tables;
		tables = x87_build_biglit_table (gwdata, tables);
	    }

#endif

/* Return "impossible" internal errors from building tables */

	    if (gwdata->GWERROR) return (gwdata->GWERROR);

/* Finish verifying table size */

#ifdef GDEBUG
	    if (!gwdata->ZERO_PADDED_FFT && !gwdata->RATIONAL_FFT) {
		char buf[80];
		intptr_t mem_used = (intptr_t) tables - (intptr_t) gwdata->gwnum_memory;
		if (mem_used != mem_needed) {
			// AVX-512 FFTs (due to rounding compressed fudges/biglits within each variable block to a cache line boundary)
			// may allocate a little too much memory when clm != 1.  Allow a small discrepancy without complaining.
			if (gwdata->cpu_flags & CPU_AVX512F && gwdata->PASS1_CACHE_LINES > 8) {
				if ((int) mem_used > (int) mem_needed || (int) mem_used < (int) (mem_needed * .98)) {
					sprintf (buf, "FFTlen: %d, mem needed should be approximately: %d\n",
						 (int) gwdata->FFTLEN, (int) (mem_used - gwdata->SCRATCH_SIZE));
					OutputBoth (0, buf);
				}
			} else {
				sprintf (buf, "FFTlen: %d, mem needed should be: %d\n",
					 (int) gwdata->FFTLEN, (int) (mem_used - gwdata->SCRATCH_SIZE));
				OutputBoth (0, buf);
			}
		}
	    }
#endif

/* Init the asm_data carries table */

	    init_asm_data_carries (gwdata, asm_data);
	}

/* Copy the count of cache lines grouped in pass 1.  This affects how we build the normalization tables. */
/* Note that cache line sizes are different in the x87 (16 bytes) and AVX-512/AVX/SSE2 code (64 bytes). */
/* In the assembly code this is called clm or cache_line_multiplier. */

	asm_data->cache_line_multiplier = gwdata->PASS1_CACHE_LINES;

/* The sumout value is FFTLEN/2 times larger than it should be.  Create an */
/* inverse to properly calculate the sumout when a multiplication ends. */

	asm_data->ttmp_ff_inv = 2.0 / (double) gwdata->FFTLEN;

/* Now compute a number of constants the assembly code needs.  These will be copied to properly aligned (SSE2 constants */
/* must be on 16-byte boundaries, AVX constants must be on 32-byte boundaries, AVX-512 are unaligned due to cheap vbroadcastsd) */
/* and grouped (for better cache line locality) assembly global variables. */

	if (!gwdata->information_only) {
	    gwasm_constants ((double *) &asm_values);

/* Init constants.  Some of these could be true global variables but I elected not */
/* to so that constants could be close to other variables used at the */
/* same time.  This might free up a cache line or two. */

/* Init constants for AVX-512 FFT routines */

	    if (gwdata->cpu_flags & CPU_AVX512F) {
		asm_data->u.zmm.ZMM_HALF = 0.5;
		asm_data->u.zmm.ZMM_TWO = 2.0;
		asm_data->u.zmm.ZMM_ONE = 1.0;
		asm_data->u.zmm.ZMM_SQRTHALF = sqrt (0.5);
		asm_data->u.zmm.ZMM_SQRT2 = sqrt (2.0);
		asm_data->u.zmm.ZMM_ABSVAL[0] = 0xFFFFFFFF;
		asm_data->u.zmm.ZMM_ABSVAL[1] = 0x7FFFFFFF;
		asm_data->u.zmm.ZMM_B = (double) b;
		asm_data->u.zmm.ZMM_ONE_OVER_B = 1.0 / asm_data->u.zmm.ZMM_B;

/* Negate c and store as a double */

		asm_data->u.zmm.ZMM_MINUS_C = (double) -c;

/* Split k for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k if k * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (k * big_word < 562949953421312.0) {
			asm_data->u.zmm.ZMM_K_LO = k;
			asm_data->u.zmm.ZMM_K_HI_OVER_SMALL_BASE = 0.0;
			asm_data->u.zmm.ZMM_K_HI_OVER_LARGE_BASE = 0.0;
		} else {
			asm_data->u.zmm.ZMM_K_HI_OVER_LARGE_BASE = floor (k / big_word);
			asm_data->u.zmm.ZMM_K_HI_OVER_SMALL_BASE = floor (k / big_word) * (double) b;
			asm_data->u.zmm.ZMM_K_LO = k - asm_data->u.zmm.ZMM_K_HI_OVER_LARGE_BASE * big_word;
		}
		gwsetmulbyconst (gwdata, gwdata->maxmulbyconst);

/* Copy computed assembly constants */

		asm_data->u.zmm.ZMM_P951 = asm_values[0];
		asm_data->u.zmm.ZMM_P588_P951 = asm_values[1];
		asm_data->u.zmm.ZMM_P309 = asm_values[2];
		asm_data->u.zmm.ZMM_P809 = -asm_values[6];
		asm_data->u.zmm.ZMM_P866 = asm_values[8];
		asm_data->u.zmm.ZMM_P383 = asm_values[21];
		asm_data->u.zmm.ZMM_P383_1 = asm_values[21];
		asm_data->u.zmm.ZMM_P434_P975 = asm_values[12];
		asm_data->u.zmm.ZMM_P782_P975 = asm_values[42];
		asm_data->u.zmm.ZMM_P901_P975 = asm_values[38];
		asm_data->u.zmm.ZMM_P623_P975 = asm_values[39];
		asm_data->u.zmm.ZMM_P223_P975 = asm_values[40];
		asm_data->u.zmm.ZMM_P1_P975 = asm_values[41];
		asm_data->u.zmm.ZMM_P259_P707 = asm_values[28];
		asm_data->u.zmm.ZMM_P966_P707 = asm_values[29];
		asm_data->u.zmm.ZMM_P924_P383 = asm_values[30];
		asm_data->u.zmm.ZMM_P981_P195 = asm_values[31];
		asm_data->u.zmm.ZMM_P195 = asm_values[33];
		asm_data->u.zmm.ZMM_P831_P556 = asm_values[34];
		asm_data->u.zmm.ZMM_P556_P195 = asm_values[37];

/* Swizzling indices used in vpermt2pd */

		asm_data->u.zmm.ZMM_PERMUTE1 = 0x0c040e0608000a02ULL;
		asm_data->u.zmm.ZMM_PERMUTE2 = 0x0d050f0709010b03ULL;
	    }

/* Init constants for AVX FFT routines */

	    else if (gwdata->cpu_flags & CPU_AVX) {
		int	i;

		asm_data->u.ymm.YMM_HALF[0] = asm_data->u.ymm.YMM_HALF[1] =
		asm_data->u.ymm.YMM_HALF[2] = asm_data->u.ymm.YMM_HALF[3] = 0.5;
		asm_data->u.ymm.YMM_ONE[0] = asm_data->u.ymm.YMM_ONE[1] =
		asm_data->u.ymm.YMM_ONE[2] = asm_data->u.ymm.YMM_ONE[3] = 1.0;
		asm_data->u.ymm.YMM_TWO[0] = asm_data->u.ymm.YMM_TWO[1] =
		asm_data->u.ymm.YMM_TWO[2] = asm_data->u.ymm.YMM_TWO[3] = 2.0;
		asm_data->u.ymm.YMM_SQRTHALF[0] = asm_data->u.ymm.YMM_SQRTHALF[1] =
		asm_data->u.ymm.YMM_SQRTHALF[2] = asm_data->u.ymm.YMM_SQRTHALF[3] = sqrt (0.5);
		asm_data->u.ymm.YMM_ABSVAL[0] = asm_data->u.ymm.YMM_ABSVAL[2] =
		asm_data->u.ymm.YMM_ABSVAL[4] = asm_data->u.ymm.YMM_ABSVAL[6] = 0xFFFFFFFF;
		asm_data->u.ymm.YMM_ABSVAL[1] = asm_data->u.ymm.YMM_ABSVAL[3] =
		asm_data->u.ymm.YMM_ABSVAL[5] = asm_data->u.ymm.YMM_ABSVAL[7] = 0x7FFFFFFF;

/* Compute the AVX (53-bit) rounding constants */

		asm_data->u.ymm.YMM_BIGVAL[0] = asm_data->u.ymm.YMM_BIGVAL[1] =
		asm_data->u.ymm.YMM_BIGVAL[2] = asm_data->u.ymm.YMM_BIGVAL[3] = 3.0 * pow (2.0, 51.0);
		asm_data->u.ymm.YMM_BIGBIGVAL[0] = asm_data->u.ymm.YMM_BIGBIGVAL[1] =
		asm_data->u.ymm.YMM_BIGBIGVAL[2] = asm_data->u.ymm.YMM_BIGBIGVAL[3] = big_word * asm_data->u.ymm.YMM_BIGVAL[0];

/* Negate c and store as a double */

		asm_data->u.ymm.YMM_MINUS_C[0] = asm_data->u.ymm.YMM_MINUS_C[1] =
		asm_data->u.ymm.YMM_MINUS_C[2] = asm_data->u.ymm.YMM_MINUS_C[3] = (double) -c;

/* Compute constant that converts fft_weight_over_fftlen found in the */
/* two-to-minus-phi tables into the true fft_weight value.  This is usually */
/* FFTLEN / 2, but when doing a non-zero-padded FFT this is FFTLEN / 2k. */

		asm_data->u.ymm.YMM_NORM012_FF[0] = asm_data->u.ymm.YMM_NORM012_FF[1] =
		asm_data->u.ymm.YMM_NORM012_FF[2] = asm_data->u.ymm.YMM_NORM012_FF[3] =
			(gwdata->ZERO_PADDED_FFT) ?
				(double) (gwdata->FFTLEN / 2) :
				(double) (gwdata->FFTLEN / 2) / k;

/* Split k for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k if k * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (k * big_word < 562949953421312.0) {
			asm_data->u.ymm.YMM_K_HI[0] = asm_data->u.ymm.YMM_K_HI[1] =
			asm_data->u.ymm.YMM_K_HI[2] = asm_data->u.ymm.YMM_K_HI[3] = 0.0;
			asm_data->u.ymm.YMM_K_LO[0] = asm_data->u.ymm.YMM_K_LO[1] =
			asm_data->u.ymm.YMM_K_LO[2] = asm_data->u.ymm.YMM_K_LO[3] = k;
		} else {
			asm_data->u.ymm.YMM_K_HI[0] = asm_data->u.ymm.YMM_K_HI[1] =
			asm_data->u.ymm.YMM_K_HI[2] = asm_data->u.ymm.YMM_K_HI[3] = floor (k / big_word) * big_word;
			asm_data->u.ymm.YMM_K_LO[0] = asm_data->u.ymm.YMM_K_LO[1] =
			asm_data->u.ymm.YMM_K_LO[2] = asm_data->u.ymm.YMM_K_LO[3] = k - asm_data->u.ymm.YMM_K_HI[0];
		}
		gwsetmulbyconst (gwdata, gwdata->maxmulbyconst);

/* Compute the normalization constants indexed by biglit array entries.  This simple */
/* method is used in one pass FFTs. */

		for (i = 0; i <= 63; i++) {
			int	ymmword, bit;
			ymmword = i >> 2;
			bit = i & 3;
			if (! (ymmword & (1 << bit))) {		/* A small word */
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i] = 1.0 / small_word;	/* Lower limit inverse */
				if (b > 2) temp = small_word;					/* Compute lower limit bigmax */
				else temp = small_word * asm_data->u.ymm.YMM_BIGVAL[0] - asm_data->u.ymm.YMM_BIGVAL[0];
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i] = temp;
			} else {				/* A big word */
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i] = 1.0 / big_word;		/* Upper limit inverse */
				if (b > 2) temp = big_word;					/* Compute upper limit bigmax */
				else temp = big_word * asm_data->u.ymm.YMM_BIGVAL[0] - asm_data->u.ymm.YMM_BIGVAL[0];
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i] = temp;
			}
		}

/* Two-pass FFTs use a clever mechanism to reduce big/lit flags data. */
/* Output the valid combinations that were precomputed in r4dwpn_build_biglit_table. */

		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
			double	inv1, inv2, bmax1, bmax2;

			inv1 = asm_data->u.ymm.YMM_LIMIT_INVERSE[0];
			inv2 = asm_data->u.ymm.YMM_LIMIT_INVERSE[63];
			bmax1 = asm_data->u.ymm.YMM_LIMIT_BIGMAX[0];
			bmax2 = asm_data->u.ymm.YMM_LIMIT_BIGMAX[63];
			for (i = 0; i < 48; i++) {
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i*4] = (((char *)gwdata->ASM_TIMERS)[i] & 1) ? inv2 : inv1;
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i*4+1] = (((char *)gwdata->ASM_TIMERS)[i] & 2) ? inv2 : inv1;
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i*4+2] = (((char *)gwdata->ASM_TIMERS)[i] & 4) ? inv2 : inv1;
				asm_data->u.ymm.YMM_LIMIT_INVERSE[i*4+3] = (((char *)gwdata->ASM_TIMERS)[i] & 8) ? inv2 : inv1;
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i*4] = (((char *)gwdata->ASM_TIMERS)[i] & 1) ? bmax2 : bmax1;
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i*4+1] = (((char *)gwdata->ASM_TIMERS)[i] & 2) ? bmax2 : bmax1;
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i*4+2] = (((char *)gwdata->ASM_TIMERS)[i] & 4) ? bmax2 : bmax1;
				asm_data->u.ymm.YMM_LIMIT_BIGMAX[i*4+3] = (((char *)gwdata->ASM_TIMERS)[i] & 8) ? bmax2 : bmax1;
			}
		}

/* Copy computed assembly constants */

		asm_data->u.ymm.YMM_P951[0] = asm_data->u.ymm.YMM_P951[1] =
		asm_data->u.ymm.YMM_P951[2] = asm_data->u.ymm.YMM_P951[3] = asm_values[0];
		asm_data->u.ymm.YMM_P618[0] = asm_data->u.ymm.YMM_P618[1] =
		asm_data->u.ymm.YMM_P618[2] = asm_data->u.ymm.YMM_P618[3] = asm_values[1];
		asm_data->u.ymm.YMM_P309[0] = asm_data->u.ymm.YMM_P309[1] =
		asm_data->u.ymm.YMM_P309[2] = asm_data->u.ymm.YMM_P309[3] = asm_values[2];
		asm_data->u.ymm.YMM_P588[0] = asm_data->u.ymm.YMM_P588[1] =
		asm_data->u.ymm.YMM_P588[2] = asm_data->u.ymm.YMM_P588[3] = asm_values[4];
		asm_data->u.ymm.YMM_P809[0] = asm_data->u.ymm.YMM_P809[1] =
		asm_data->u.ymm.YMM_P809[2] = asm_data->u.ymm.YMM_P809[3] = -asm_values[6];
		asm_data->u.ymm.YMM_P866[0] = asm_data->u.ymm.YMM_P866[1] =
		asm_data->u.ymm.YMM_P866[2] = asm_data->u.ymm.YMM_P866[3] = asm_values[8];
		asm_data->u.ymm.YMM_P975[0] = asm_data->u.ymm.YMM_P975[1] =
		asm_data->u.ymm.YMM_P975[2] = asm_data->u.ymm.YMM_P975[3] = asm_values[11];
		asm_data->u.ymm.YMM_P623[0] = asm_data->u.ymm.YMM_P623[1] =
		asm_data->u.ymm.YMM_P623[2] = asm_data->u.ymm.YMM_P623[3] = asm_values[14];
		asm_data->u.ymm.YMM_P223[0] = asm_data->u.ymm.YMM_P223[1] =
		asm_data->u.ymm.YMM_P223[2] = asm_data->u.ymm.YMM_P223[3] = -asm_values[17];
		asm_data->u.ymm.YMM_P901[0] = asm_data->u.ymm.YMM_P901[1] =
		asm_data->u.ymm.YMM_P901[2] = asm_data->u.ymm.YMM_P901[3] = -asm_values[18];
		asm_data->u.ymm.YMM_P924[0] = asm_data->u.ymm.YMM_P924[1] =
		asm_data->u.ymm.YMM_P924[2] = asm_data->u.ymm.YMM_P924[3] = asm_values[20];
		asm_data->u.ymm.YMM_P383[0] = asm_data->u.ymm.YMM_P383[1] =
		asm_data->u.ymm.YMM_P383[2] = asm_data->u.ymm.YMM_P383[3] = asm_values[21];
		asm_data->u.ymm.YMM_P782[0] = asm_data->u.ymm.YMM_P782[1] =
		asm_data->u.ymm.YMM_P782[2] = asm_data->u.ymm.YMM_P782[3] = asm_values[22];
		asm_data->u.ymm.YMM_P434[0] = asm_data->u.ymm.YMM_P434[1] =
		asm_data->u.ymm.YMM_P434[2] = asm_data->u.ymm.YMM_P434[3] = asm_values[23];
		asm_data->u.ymm.YMM_P975_P434[0] = asm_data->u.ymm.YMM_P975_P434[1] =
		asm_data->u.ymm.YMM_P975_P434[2] = asm_data->u.ymm.YMM_P975_P434[3] = asm_values[24];
		asm_data->u.ymm.YMM_P782_P434[0] = asm_data->u.ymm.YMM_P782_P434[1] =
		asm_data->u.ymm.YMM_P782_P434[2] = asm_data->u.ymm.YMM_P782_P434[3] = asm_values[25];
		asm_data->u.ymm.YMM_P259[0] = asm_data->u.ymm.YMM_P259[1] =
		asm_data->u.ymm.YMM_P259[2] = asm_data->u.ymm.YMM_P259[3] = asm_values[26];
		asm_data->u.ymm.YMM_P966[0] = asm_data->u.ymm.YMM_P966[1] =
		asm_data->u.ymm.YMM_P966[2] = asm_data->u.ymm.YMM_P966[3] = asm_values[27];
		asm_data->u.ymm.YMM_P259_P707[0] = asm_data->u.ymm.YMM_P259_P707[1] =
		asm_data->u.ymm.YMM_P259_P707[2] = asm_data->u.ymm.YMM_P259_P707[3] = asm_values[28];
		asm_data->u.ymm.YMM_P966_P707[0] = asm_data->u.ymm.YMM_P966_P707[1] =
		asm_data->u.ymm.YMM_P966_P707[2] = asm_data->u.ymm.YMM_P966_P707[3] = asm_values[29];
		asm_data->u.ymm.YMM_P924_P383[0] = asm_data->u.ymm.YMM_P924_P383[1] =
		asm_data->u.ymm.YMM_P924_P383[2] = asm_data->u.ymm.YMM_P924_P383[3] = asm_values[30];
	    }

/* Init constants for SSE2 code */

	    else if (gwdata->cpu_flags & CPU_SSE2) {
		asm_data->u.xmm.XMM_TWO[0] = asm_data->u.xmm.XMM_TWO[1] = 2.0;
		asm_data->u.xmm.XMM_HALF[0] = asm_data->u.xmm.XMM_HALF[1] = 0.5;
		asm_data->u.xmm.XMM_SQRTHALF[0] = asm_data->u.xmm.XMM_SQRTHALF[1] = sqrt (0.5);
		asm_data->u.xmm.XMM_ABSVAL[0] = asm_data->u.xmm.XMM_ABSVAL[2] = 0xFFFFFFFF;
		asm_data->u.xmm.XMM_ABSVAL[1] = asm_data->u.xmm.XMM_ABSVAL[3] = 0x7FFFFFFF;

		asm_data->u.xmm.XMM_TTP_FUDGE[0] = asm_data->u.xmm.XMM_TTP_FUDGE[1] =
		asm_data->u.xmm.XMM_TTP_FUDGE[2] = asm_data->u.xmm.XMM_TTP_FUDGE[3] =
		asm_data->u.xmm.XMM_TTP_FUDGE[4] = asm_data->u.xmm.XMM_TTP_FUDGE[5] =
		asm_data->u.xmm.XMM_TTP_FUDGE[6] = asm_data->u.xmm.XMM_TTP_FUDGE[7] =
		asm_data->u.xmm.XMM_TTP_FUDGE[9] = asm_data->u.xmm.XMM_TTP_FUDGE[11] =
		asm_data->u.xmm.XMM_TTP_FUDGE[13] = asm_data->u.xmm.XMM_TTP_FUDGE[15] =
		asm_data->u.xmm.XMM_TTP_FUDGE[16] = asm_data->u.xmm.XMM_TTP_FUDGE[18] =
		asm_data->u.xmm.XMM_TTP_FUDGE[20] = asm_data->u.xmm.XMM_TTP_FUDGE[22] = 1.0;
		asm_data->u.xmm.XMM_TTP_FUDGE[8] = asm_data->u.xmm.XMM_TTP_FUDGE[10] =
		asm_data->u.xmm.XMM_TTP_FUDGE[12] = asm_data->u.xmm.XMM_TTP_FUDGE[14] =
		asm_data->u.xmm.XMM_TTP_FUDGE[17] = asm_data->u.xmm.XMM_TTP_FUDGE[19] =
		asm_data->u.xmm.XMM_TTP_FUDGE[21] = asm_data->u.xmm.XMM_TTP_FUDGE[23] =
		asm_data->u.xmm.XMM_TTP_FUDGE[24] = asm_data->u.xmm.XMM_TTP_FUDGE[25] =
		asm_data->u.xmm.XMM_TTP_FUDGE[26] = asm_data->u.xmm.XMM_TTP_FUDGE[27] =
		asm_data->u.xmm.XMM_TTP_FUDGE[28] = asm_data->u.xmm.XMM_TTP_FUDGE[29] =
		asm_data->u.xmm.XMM_TTP_FUDGE[30] = asm_data->u.xmm.XMM_TTP_FUDGE[31] = 1.0 / (double) b;

		asm_data->u.xmm.XMM_TTMP_FUDGE[0] = asm_data->u.xmm.XMM_TTMP_FUDGE[1] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[2] = asm_data->u.xmm.XMM_TTMP_FUDGE[3] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[4] = asm_data->u.xmm.XMM_TTMP_FUDGE[5] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[6] = asm_data->u.xmm.XMM_TTMP_FUDGE[7] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[9] = asm_data->u.xmm.XMM_TTMP_FUDGE[11] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[13] = asm_data->u.xmm.XMM_TTMP_FUDGE[15] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[16] = asm_data->u.xmm.XMM_TTMP_FUDGE[18] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[20] = asm_data->u.xmm.XMM_TTMP_FUDGE[22] = 1.0;
		asm_data->u.xmm.XMM_TTMP_FUDGE[8] = asm_data->u.xmm.XMM_TTMP_FUDGE[10] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[12] = asm_data->u.xmm.XMM_TTMP_FUDGE[14] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[17] = asm_data->u.xmm.XMM_TTMP_FUDGE[19] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[21] = asm_data->u.xmm.XMM_TTMP_FUDGE[23] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[24] = asm_data->u.xmm.XMM_TTMP_FUDGE[25] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[26] = asm_data->u.xmm.XMM_TTMP_FUDGE[27] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[28] = asm_data->u.xmm.XMM_TTMP_FUDGE[29] =
		asm_data->u.xmm.XMM_TTMP_FUDGE[30] = asm_data->u.xmm.XMM_TTMP_FUDGE[31] = (double) b;

/* Compute the SSE2 (53-bit) rounding constants */

		asm_data->u.xmm.XMM_BIGVAL[0] = asm_data->u.xmm.XMM_BIGVAL[1] = 3.0 * pow (2.0, 51.0);
		asm_data->u.xmm.XMM_BIGVAL_NEG[0] = asm_data->u.xmm.XMM_BIGVAL_NEG[1] = -asm_data->u.xmm.XMM_BIGVAL[0];
		asm_data->u.xmm.XMM_BIGBIGVAL[0] = asm_data->u.xmm.XMM_BIGBIGVAL[1] = big_word * asm_data->u.xmm.XMM_BIGVAL[0];

/* Negate c and store as a double */

		asm_data->u.xmm.XMM_MINUS_C[0] = asm_data->u.xmm.XMM_MINUS_C[1] = (double) -c;

/* Compute constant that converts fft_weight_over_fftlen found in the */
/* two-to-minus-phi tables into the true fft_weight value.  This is usually */
/* FFTLEN / 2, but when doing a non-zero-padded FFT this is FFTLEN / 2k. */

		asm_data->u.xmm.XMM_NORM012_FF[0] = asm_data->u.xmm.XMM_NORM012_FF[1] =
			(gwdata->ZERO_PADDED_FFT) ?
				(double) (gwdata->FFTLEN / 2) :
				(double) (gwdata->FFTLEN / 2) / k;

/* Split k for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k if k * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (k * big_word < 562949953421312.0) {
			asm_data->u.xmm.XMM_K_HI[0] = asm_data->u.xmm.XMM_K_HI[1] = 0.0;
			asm_data->u.xmm.XMM_K_LO[0] = asm_data->u.xmm.XMM_K_LO[1] = k;
		} else {
			asm_data->u.xmm.XMM_K_HI[0] = asm_data->u.xmm.XMM_K_HI[1] = floor (k / big_word) * big_word;
			asm_data->u.xmm.XMM_K_LO[0] = asm_data->u.xmm.XMM_K_LO[1] = k - asm_data->u.xmm.XMM_K_HI[0];
		}
		gwsetmulbyconst (gwdata, gwdata->maxmulbyconst);

/* Compute the normalization constants indexed by biglit array entries */

		asm_data->u.xmm.XMM_LIMIT_INVERSE[0] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[1] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[3] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[4] = 1.0 / small_word;	/* Lower limit inverse */
		asm_data->u.xmm.XMM_LIMIT_INVERSE[2] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[5] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[6] =
		asm_data->u.xmm.XMM_LIMIT_INVERSE[7] = 1.0 / big_word;		/* Upper limit inverse */

		/* Compute lower limit bigmax */
		if (b > 2)
			temp = small_word;
		else
			temp = small_word * asm_data->u.xmm.XMM_BIGVAL[0] - asm_data->u.xmm.XMM_BIGVAL[0];
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[0] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[1] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[3] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[4] = temp;

		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[0] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[1] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[3] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[4] = -temp;	/* Negative lower limit bigmax */

		/* Compute upper limit bigmax */
		if (b > 2)
			temp = big_word;
		else
			temp = big_word * asm_data->u.xmm.XMM_BIGVAL[0] - asm_data->u.xmm.XMM_BIGVAL[0];
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[2] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[5] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[6] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX[7] = temp;

		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[2] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[5] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[6] =
		asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[7] = -temp;	/* Negative upper limit bigmax */

		memcpy (asm_data->u.xmm.XMM_LIMIT_INVERSE+8, asm_data->u.xmm.XMM_LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_INVERSE+16, asm_data->u.xmm.XMM_LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_INVERSE+24, asm_data->u.xmm.XMM_LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX+8, asm_data->u.xmm.XMM_LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX+16, asm_data->u.xmm.XMM_LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX+24, asm_data->u.xmm.XMM_LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG+8, asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG+16, asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG, 8 * sizeof (double));
		memcpy (asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG+24, asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG, 8 * sizeof (double));

/* Newer FFTs use a clever mechanism to reduce big/lit flags data. */
/* Output the valid combinations that were precomputed in r4dwpn_build_biglit_table. */

		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
			int	i;
			double	inv1, inv2, bmax1, bmax2;

			inv1 = asm_data->u.xmm.XMM_LIMIT_INVERSE[0];
			inv2 = asm_data->u.xmm.XMM_LIMIT_INVERSE[7];
			bmax1 = asm_data->u.xmm.XMM_LIMIT_BIGMAX[0];
			bmax2 = asm_data->u.xmm.XMM_LIMIT_BIGMAX[7];
			for (i = 0; i < 48; i++) {
				asm_data->u.xmm.XMM_LIMIT_INVERSE[i+i] = (((char *)gwdata->ASM_TIMERS)[i] & 1) ? inv2 : inv1;
				asm_data->u.xmm.XMM_LIMIT_INVERSE[i+i+1] = (((char *)gwdata->ASM_TIMERS)[i] & 2) ? inv2 : inv1;
				asm_data->u.xmm.XMM_LIMIT_BIGMAX[i+i] = (((char *)gwdata->ASM_TIMERS)[i] & 1) ? bmax2 : bmax1;
				asm_data->u.xmm.XMM_LIMIT_BIGMAX[i+i+1] = (((char *)gwdata->ASM_TIMERS)[i] & 2) ? bmax2 : bmax1;
				asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[i+i] = (((char *)gwdata->ASM_TIMERS)[i] & 1) ? -bmax2 : -bmax1;
				asm_data->u.xmm.XMM_LIMIT_BIGMAX_NEG[i+i+1] = (((char *)gwdata->ASM_TIMERS)[i] & 2) ? -bmax2 : -bmax1;
			}
		}

/* Copy computed assembly constants */

		asm_data->u.xmm.XMM_P951[0] = asm_data->u.xmm.XMM_P951[1] = asm_values[0];
		asm_data->u.xmm.XMM_P618[0] = asm_data->u.xmm.XMM_P618[1] = asm_values[1];
		asm_data->u.xmm.XMM_P309[0] = asm_data->u.xmm.XMM_P309[1] = asm_values[2];
		asm_data->u.xmm.XMM_M262[0] = asm_data->u.xmm.XMM_M262[1] = asm_values[3];
		asm_data->u.xmm.XMM_P588[0] = asm_data->u.xmm.XMM_P588[1] = asm_values[4];
		asm_data->u.xmm.XMM_M162[0] = asm_data->u.xmm.XMM_M162[1] = asm_values[5];
		asm_data->u.xmm.XMM_P809[0] = asm_data->u.xmm.XMM_P809[1] = -asm_values[6];
		asm_data->u.xmm.XMM_M809[0] = asm_data->u.xmm.XMM_M809[1] = asm_values[6];
		asm_data->u.xmm.XMM_M382[0] = asm_data->u.xmm.XMM_M382[1] = asm_values[7];
		asm_data->u.xmm.XMM_P866[0] = asm_data->u.xmm.XMM_P866[1] = asm_values[8];
		asm_data->u.xmm.XMM_P975[0] = asm_data->u.xmm.XMM_P975[1] = asm_values[11];
		asm_data->u.xmm.XMM_P445[0] = asm_data->u.xmm.XMM_P445[1] = asm_values[12];
		asm_data->u.xmm.XMM_P180[0] = asm_data->u.xmm.XMM_P180[1] = asm_values[13];
		asm_data->u.xmm.XMM_P623[0] = asm_data->u.xmm.XMM_P623[1] = asm_values[14];
		asm_data->u.xmm.XMM_M358[0] = asm_data->u.xmm.XMM_M358[1] = asm_values[15];
		asm_data->u.xmm.XMM_P404[0] = asm_data->u.xmm.XMM_P404[1] = asm_values[16];
		asm_data->u.xmm.XMM_P223[0] = asm_data->u.xmm.XMM_P223[1] = -asm_values[17];
		asm_data->u.xmm.XMM_P901[0] = asm_data->u.xmm.XMM_P901[1] = -asm_values[18];
		asm_data->u.xmm.XMM_P924[0] = asm_data->u.xmm.XMM_P924[1] = asm_values[20];
		asm_data->u.xmm.XMM_P383[0] = asm_data->u.xmm.XMM_P383[1] = asm_values[21];
		asm_data->u.xmm.XMM_P782[0] = asm_data->u.xmm.XMM_P782[1] = asm_values[22];
		asm_data->u.xmm.XMM_P434[0] = asm_data->u.xmm.XMM_P434[1] = asm_values[23];
	    }

/* Init constants for x87 code */

	    else {
		asm_data->u.x87.HALF = 0.5;
		asm_data->u.x87.SQRTHALF = sqrt (0.5);
		asm_data->u.x87.P25 = 0.25;
		asm_data->u.x87.P75 = 0.75;
		asm_data->u.x87.P3 = 3.0;

		asm_data->u.x87.TTP_FUDGE[0] = asm_data->u.x87.TTP_FUDGE[1] =
		asm_data->u.x87.TTP_FUDGE[2] = asm_data->u.x87.TTP_FUDGE[3] =
		asm_data->u.x87.TTP_FUDGE[4] = asm_data->u.x87.TTP_FUDGE[5] =
		asm_data->u.x87.TTP_FUDGE[6] = asm_data->u.x87.TTP_FUDGE[7] =
		asm_data->u.x87.TTP_FUDGE[9] = asm_data->u.x87.TTP_FUDGE[11] =
		asm_data->u.x87.TTP_FUDGE[13] = asm_data->u.x87.TTP_FUDGE[15] =
		asm_data->u.x87.TTP_FUDGE[16] = asm_data->u.x87.TTP_FUDGE[18] =
		asm_data->u.x87.TTP_FUDGE[20] = asm_data->u.x87.TTP_FUDGE[22] = 1.0;
		asm_data->u.x87.TTP_FUDGE[8] = asm_data->u.x87.TTP_FUDGE[10] =
		asm_data->u.x87.TTP_FUDGE[12] = asm_data->u.x87.TTP_FUDGE[14] =
		asm_data->u.x87.TTP_FUDGE[17] = asm_data->u.x87.TTP_FUDGE[19] =
		asm_data->u.x87.TTP_FUDGE[21] = asm_data->u.x87.TTP_FUDGE[23] =
		asm_data->u.x87.TTP_FUDGE[24] = asm_data->u.x87.TTP_FUDGE[25] =
		asm_data->u.x87.TTP_FUDGE[26] = asm_data->u.x87.TTP_FUDGE[27] =
		asm_data->u.x87.TTP_FUDGE[28] = asm_data->u.x87.TTP_FUDGE[29] =
		asm_data->u.x87.TTP_FUDGE[30] = asm_data->u.x87.TTP_FUDGE[31] = 1.0 / (double) b;

		asm_data->u.x87.TTMP_FUDGE[0] = asm_data->u.x87.TTMP_FUDGE[1] =
		asm_data->u.x87.TTMP_FUDGE[2] = asm_data->u.x87.TTMP_FUDGE[3] =
		asm_data->u.x87.TTMP_FUDGE[4] = asm_data->u.x87.TTMP_FUDGE[5] =
		asm_data->u.x87.TTMP_FUDGE[6] = asm_data->u.x87.TTMP_FUDGE[7] =
		asm_data->u.x87.TTMP_FUDGE[9] = asm_data->u.x87.TTMP_FUDGE[11] =
		asm_data->u.x87.TTMP_FUDGE[13] = asm_data->u.x87.TTMP_FUDGE[15] =
		asm_data->u.x87.TTMP_FUDGE[16] = asm_data->u.x87.TTMP_FUDGE[18] =
		asm_data->u.x87.TTMP_FUDGE[20] = asm_data->u.x87.TTMP_FUDGE[22] = 1.0;
		asm_data->u.x87.TTMP_FUDGE[8] = asm_data->u.x87.TTMP_FUDGE[10] =
		asm_data->u.x87.TTMP_FUDGE[12] = asm_data->u.x87.TTMP_FUDGE[14] =
		asm_data->u.x87.TTMP_FUDGE[17] = asm_data->u.x87.TTMP_FUDGE[19] =
		asm_data->u.x87.TTMP_FUDGE[21] = asm_data->u.x87.TTMP_FUDGE[23] =
		asm_data->u.x87.TTMP_FUDGE[24] = asm_data->u.x87.TTMP_FUDGE[25] =
		asm_data->u.x87.TTMP_FUDGE[26] = asm_data->u.x87.TTMP_FUDGE[27] =
		asm_data->u.x87.TTMP_FUDGE[28] = asm_data->u.x87.TTMP_FUDGE[29] =
		asm_data->u.x87.TTMP_FUDGE[30] = asm_data->u.x87.TTMP_FUDGE[31] = (double) b;

/* Compute the x87 (64-bit) rounding constants */

		asm_data->u.x87.BIGVAL = (float) (3.0 * pow (2.0, 62.0));
		asm_data->u.x87.BIGBIGVAL = (float) (big_word * asm_data->u.x87.BIGVAL);

/* Negate c and store as a double */

		asm_data->u.x87.MINUS_C = (double) -c;

/* Compute constant that converts fft_weight_over_fftlen found in the */
/* two-to-minus-phi tables into the true fft_weight value.  This is usually */
/* FFTLEN / 2, but when doing a non-zero-padded FFT this is FFTLEN / 2k. */

		asm_data->u.x87.NORM012_FF =
			(gwdata->ZERO_PADDED_FFT) ?
				(double) (gwdata->FFTLEN / 2) :
				(double) (gwdata->FFTLEN / 2) / k;

/* Split k for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k if k * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		asm_data->u.x87.K_HI = floor (k / big_word) * big_word;
		asm_data->u.x87.K_LO = k - asm_data->u.x87.K_HI;
		asm_data->u.x87.K_HI_2 = floor (k / big_word / big_word) * big_word * big_word;
		asm_data->u.x87.K_HI_1 = asm_data->u.x87.K_HI - asm_data->u.x87.K_HI_2;
		gwsetmulbyconst (gwdata, gwdata->maxmulbyconst);

/* Compute the normalization constants indexed by biglit array entries */

		asm_data->u.x87.LIMIT_INVERSE[0] =
		asm_data->u.x87.LIMIT_INVERSE[1] =
		asm_data->u.x87.LIMIT_INVERSE[3] =
		asm_data->u.x87.LIMIT_INVERSE[4] = 1.0 / small_word;	/* Lower limit inverse */
		asm_data->u.x87.LIMIT_INVERSE[2] =
		asm_data->u.x87.LIMIT_INVERSE[5] =
		asm_data->u.x87.LIMIT_INVERSE[6] =
		asm_data->u.x87.LIMIT_INVERSE[7] = 1.0 / big_word;	/* Upper limit inverse */

		/* Compute lower limit bigmax */
		temp = small_word * asm_data->u.x87.BIGVAL - asm_data->u.x87.BIGVAL;
		asm_data->u.x87.LIMIT_BIGMAX[0] =
		asm_data->u.x87.LIMIT_BIGMAX[1] =
		asm_data->u.x87.LIMIT_BIGMAX[3] =
		asm_data->u.x87.LIMIT_BIGMAX[4] = temp;

		asm_data->u.x87.LIMIT_BIGMAX_NEG[0] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[1] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[3] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[4] = -temp;	/* Negative lower limit bigmax */

		/* Compute upper limit bigmax */
		temp = big_word * asm_data->u.x87.BIGVAL - asm_data->u.x87.BIGVAL;
		asm_data->u.x87.LIMIT_BIGMAX[2] =
		asm_data->u.x87.LIMIT_BIGMAX[5] =
		asm_data->u.x87.LIMIT_BIGMAX[6] =
		asm_data->u.x87.LIMIT_BIGMAX[7] = temp;

		asm_data->u.x87.LIMIT_BIGMAX_NEG[2] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[5] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[6] =
		asm_data->u.x87.LIMIT_BIGMAX_NEG[7] = -temp;	/* Negative upper limit bigmax */

		memcpy (asm_data->u.x87.LIMIT_INVERSE+8, asm_data->u.x87.LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_INVERSE+16, asm_data->u.x87.LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_INVERSE+24, asm_data->u.x87.LIMIT_INVERSE, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX+8, asm_data->u.x87.LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX+16, asm_data->u.x87.LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX+24, asm_data->u.x87.LIMIT_BIGMAX, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX_NEG+8, asm_data->u.x87.LIMIT_BIGMAX_NEG, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX_NEG+16, asm_data->u.x87.LIMIT_BIGMAX_NEG, 8 * sizeof (double));
		memcpy (asm_data->u.x87.LIMIT_BIGMAX_NEG+24, asm_data->u.x87.LIMIT_BIGMAX_NEG, 8 * sizeof (double));

/* Copy computed assembly constants */

		asm_data->u.x87.P951 = asm_values[0];
		asm_data->u.x87.P618 = asm_values[1];
		asm_data->u.x87.P309 = asm_values[2];
		asm_data->u.x87.M262 = asm_values[3];
		asm_data->u.x87.P588 = asm_values[4];
		asm_data->u.x87.M162 = asm_values[5];
		asm_data->u.x87.M809 = asm_values[6];
		asm_data->u.x87.M382 = asm_values[7];
		asm_data->u.x87.P866 = asm_values[8];
		asm_data->u.x87.P433 = asm_values[9];
		asm_data->u.x87.P577 = asm_values[10];
		asm_data->u.x87.P975 = asm_values[11];
		asm_data->u.x87.P445 = asm_values[12];
		asm_data->u.x87.P180 = asm_values[13];
		asm_data->u.x87.P623 = asm_values[14];
		asm_data->u.x87.M358 = asm_values[15];
		asm_data->u.x87.P404 = asm_values[16];
		asm_data->u.x87.M223 = asm_values[17];
		asm_data->u.x87.M901 = asm_values[18];
		asm_data->u.x87.M691 = asm_values[19];
	    }
	}

/* More AVX-512 initialization code */

	if (gwdata->cpu_flags & CPU_AVX512F) {

		/* One pass AVX-512 FFTs use the same 4KB padding scheme as two-pass FFTs */
		if (gwdata->PASS1_SIZE == 0) {
			asm_data->fourKBgapsize = gwdata->FOURKBGAPSIZE;
			asm_data->normblkdst = 0;		/* Small pass 1's with PFA have no padding every 8 clmblkdsts */
			asm_data->normblkdst4 = gwdata->FOURKBGAPSIZE;
			asm_data->normblkdst8 = 0;
			asm_data->pass1blkdst = 8*128;		/* Allows one-pass FFTs to use two-pass carry propagate code */
			asm_data->pass2blkdst = 128;		/* Allows one-pass FFTs to use two-pass carry propagate code */
			asm_data->normval4 = 1;			/* Allows one-pass FFTs to use two-pass add/sub/addsub code */
			asm_data->normval2 = 1;			/* Allows one-pass FFTs to use two-pass add/sub/addsub code */
			asm_data->normval1 = 1;			/* Allows one-pass FFTs to use two-pass add/sub/addsub quick code */
			asm_data->pass2gapsize = 0;		/* Allows one-pass FFTs to use two-pass add/sub/addsub quick code */
		}

		/* Calculate padding constants for two-pass FFTs */
		else {
			int	pass1_size;

			/* There are 64 pad bytes every 4KB and optional pad bytes between each pass 1 block. */
			asm_data->fourKBgapsize = gwdata->FOURKBGAPSIZE;
			asm_data->pass2gapsize = gwdata->PASS2GAPSIZE;
			asm_data->pass2blkdst = gwdata->PASS2_SIZE * 16;
			asm_data->pass2blkdst += (asm_data->pass2blkdst >> 12) * asm_data->fourKBgapsize + asm_data->pass2gapsize;
			asm_data->pass1blkdst = asm_data->pass2blkdst * 8;

			/* Calculate normblk distances used in normalizing two-pass FFTs */
			pass1_size = gwdata->PASS1_SIZE;
			if (pass1_size == 192 || pass1_size == 640 || pass1_size == 768 || pass1_size == 896 || pass1_size == 960 ||
			    pass1_size == 1152 || pass1_size == 1280 || pass1_size == 1344 || pass1_size == 1536 || pass1_size == 1920 ||
			    pass1_size == 2304) {
				asm_data->normblkdst = 64;		/* Many pass 1's do not support padding every 8 clmblkdsts */
				asm_data->normblkdst8 = 0;
			} else if (gwdata->PASS1_CACHE_LINES == 8) {	/* If clm=1 */
				asm_data->normblkdst = 0;		/* Pad for clmblkdst is 64 bytes */
				asm_data->normblkdst8 = 128;		/* Pad for clmblkdst8 is 64 bytes */
			} else if (gwdata->PASS1_CACHE_LINES == 16) {	/* If clm=2 */
				asm_data->normblkdst = 0;		/* Pad for clmblkdst is 0 bytes */
				asm_data->normblkdst8 = 192;		/* Pad for clmblkdst8 is 192 bytes */
			} else {					/* If clm=4 or more */
				asm_data->normblkdst = 64;		/* Pad for clmblkdst is 64 bytes */
				asm_data->normblkdst8 = -64;		/* Pad for clmblkdst8 is -64 bytes */
			}
			asm_data->normblkdst4 = 0;

			/* If we are padding every 4KB, then calculate number of 4KB pages in a pass 2 block */
			/* and "FFT loword/hiword pairs in a 4KB page. */
			/* Otherwise calculate "FFT loword/hiword pairs in a pass 2 block" */
			if (gwdata->FOURKBGAPSIZE) {
				asm_data->normval4 = (gwdata->PASS2_SIZE * 16) / 4096;
				asm_data->normval1 = 4096 / 128;
			} else {
				asm_data->normval4 = 1;
				asm_data->normval1 = gwdata->PASS2_SIZE * 16 / 128;
			}
			/* When adjusting big/lit ptr in add/sub/addsub/smallmul routines, we instead need to know */
			/* "number of clms in a 4KB page".  We also need to calculate the amount to bump the big/lit pointer. */
			asm_data->normval2 = asm_data->normval1 / (asm_data->cache_line_multiplier / 8);
			if (gwdata->ZERO_PADDED_FFT) asm_data->normval3 = gwdata->pass1_var_data_size -  asm_data->cache_line_multiplier / 2;
			else asm_data->normval3 = gwdata->pass1_var_data_size - asm_data->cache_line_multiplier;
		}
	}

/* More AVX initialization code */

	else if (gwdata->cpu_flags & CPU_AVX) {

		if (gwdata->PASS2_SIZE) {
			int	pass1_size;

			/* There are 64 pad bytes every 4KB and optional */
			/* pad bytes between each pass 1 block. */
			asm_data->fourKBgapsize = gwdata->FOURKBGAPSIZE;
			asm_data->pass2gapsize = gwdata->PASS2GAPSIZE;
			asm_data->pass2blkdst = gwdata->PASS2_SIZE * 16;
			asm_data->pass2blkdst += (asm_data->pass2blkdst >> 12) * asm_data->fourKBgapsize + asm_data->pass2gapsize;
			asm_data->pass1blkdst = asm_data->pass2blkdst * 4;

			/* Calculate normblk distances used in normalizing two-pass FFTs */
			pass1_size = gwdata->PASS1_SIZE;
			if ((pass1_size == 384 && gwdata->NEGACYCLIC_FFT) ||
			    (pass1_size == 640 && gwdata->NEGACYCLIC_FFT) ||
			    (pass1_size == 1536 && gwdata->NEGACYCLIC_FFT)) {
				asm_data->normblkdst = 64;		/* Small pass 1's with PFA have no padding every 8 clmblkdsts */
				asm_data->normblkdst8 = 0;
			} else if (gwdata->PASS1_CACHE_LINES == 4) {	/* If clm=1 */
				asm_data->normblkdst = 0;		/* Pad for clmblkdst is 64 bytes */
				asm_data->normblkdst8 = 64;		/* Pad for clmblkdst8 is 64 bytes */
			} else if (gwdata->PASS1_CACHE_LINES == 8) {	/* If clm=2 */
				asm_data->normblkdst = 0;		/* Pad for clmblkdst is 0 bytes */
				asm_data->normblkdst8 = 192;		/* Pad for clmblkdst8 is 192 bytes */
			} else {					/* If clm=4 or more */
				asm_data->normblkdst = 64;		/* Pad for clmblkdst is 64 bytes */
				asm_data->normblkdst8 = -64;		/* Pad for clmblkdst8 is -64 bytes */
			}

			/* If we are padding 4KB pages, then calculate the number of 4KB pages in a pass 2 block */
			/* and "cache lines in a 4KB page" / clm. */
			/* Otherwise calculate "cache lines in a pass 2 block" / clm. */
			if (gwdata->FOURKBGAPSIZE) {
				asm_data->normval4 = (gwdata->PASS2_SIZE * 16) >> 12;
				asm_data->normval1 = (4096 / 64) / (asm_data->cache_line_multiplier / 4);
			} else {
				asm_data->normval4 = 1;
				asm_data->normval1 = (gwdata->PASS2_SIZE * 16 / 64) / (asm_data->cache_line_multiplier / 4);
			}

			/* Big/lit flags ptr fudge factor in add/sub */
			asm_data->normval2 = asm_data->cache_line_multiplier * 2 * (asm_data->addcount1 - 1);

			/* Big/lit flags ptr fudge factor #2 in add/sub */
			asm_data->normval3 = asm_data->cache_line_multiplier * 2;
		}
	}

/* SSE2 initialization code formerly done in assembly language */

	else if (gwdata->cpu_flags & CPU_SSE2) {

		/* Data size = pass2_size complex numbers * 2 (for SSE2) */
		/* Calculate number of 8KB pages */
		asm_data->normval4 = (gwdata->PASS2_SIZE * 16 * 2) >> 13;

		/* Pass 2 block distance includes 128 pad bytes every 8KB */
		asm_data->pass2gapsize = gwdata->PASS2GAPSIZE;
		asm_data->pass2blkdst = gwdata->PASS2_SIZE * 2 * 8 * 2;
		asm_data->pass2blkdst += (asm_data->pass2blkdst >> 13) * 128 + asm_data->pass2gapsize;
		asm_data->pass1blkdst = asm_data->pass2blkdst;

		/* Calculate normblk distances */
		if (gwdata->SCRATCH_SIZE == 0) {
			/* Normblkdst = pass1blkdst - clm*64 */
			asm_data->normblkdst = asm_data->pass1blkdst - asm_data->cache_line_multiplier * 64;
			asm_data->normblkdst8 = 0;
		} else if ((gwdata->FFT_TYPE == FFT_TYPE_RADIX_4 ||
			    gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DELAYED ||
			    gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) &&
			   (gwdata->PASS1_SIZE == 80 ||
			    gwdata->PASS1_SIZE == 96 ||
			    gwdata->PASS1_SIZE == 112 ||
			    gwdata->PASS1_SIZE == 224)) {
			/* Small pass 1's with PFA have no padding every 8 clmblkdsts */
			asm_data->normblkdst = 0;
			asm_data->normblkdst8 = 0;
		} else {
			/* Pad in clmblkdst is zero, clmblkdst8 is 128 */
			asm_data->normblkdst = 0;
			asm_data->normblkdst8 = 128;
		}

		if (gwdata->PASS2_SIZE) {
			/* Calculate "cache lines in a chunk" / clm */
			asm_data->normval1 = (8192/64) / asm_data->cache_line_multiplier;

			/* Big/lit flags ptr fudge factor in add/sub */
			asm_data->normval2 = asm_data->cache_line_multiplier * 4 * (asm_data->addcount1 - 1);

			/* Big/lit flags ptr fudge factor #2 in add/sub */
			asm_data->normval3 = asm_data->cache_line_multiplier * 4 -
					     (asm_data->cache_line_multiplier * 4 +
					      asm_data->normval2) *
					     asm_data->normval1 * asm_data->normval4;

			/* This FFT type uses only half the big/lit data */
			if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
				asm_data->normval2 /= 2;
				asm_data->normval3 /= 2;
			}
		}
	}

/* x87 initialization. Calculate constants used in two pass FFTs. */
/* Foremost is the pass 1 blkdst and normalize blkdst for auxiliary mult */
/* routines.  The two values are the same except for larger FFTs which */
/* use a scratch area. */

	else {
		if (gwdata->PASS2_SIZE) {
			/* Pass 2 data size: pass2_size complex numbers */
			/* Compute number of 4KB pages, used in normalized */
			/* add/sub */
			asm_data->normval4 = (gwdata->PASS2_SIZE * 16) >> 12;

			/* pad 64 bytes every 4KB + 64 bytes between blocks */
			asm_data->pass2blkdst =
		        asm_data->pass1blkdst =
			asm_data->normblkdst = asm_data->normval4 * (4096+64) + 64;
			asm_data->normblkdst8 = 0;

			if (gwdata->SCRATCH_SIZE) {
				/* Compute scratch area normblkdst */
				asm_data->normblkdst = asm_data->cache_line_multiplier * 32;

				/* Only larger pass1's have padding */
				if (asm_data->addcount1 >= 128) asm_data->normblkdst8 = 64;
			}

			/* Compute "cache lines in a page" / clm */
			asm_data->normval1 = (4096/32) / asm_data->cache_line_multiplier;

			/* Big/lit flags ptr fudge factor in add/sub */
			asm_data->normval2 = asm_data->cache_line_multiplier * 2 * (asm_data->addcount1 - 1);

			/* Big/lit flags ptr fudge factor #2 in add/sub */
			asm_data->normval3 = asm_data->cache_line_multiplier * 2 -
					     ((asm_data->cache_line_multiplier * 2 +
					       asm_data->normval2) *
					      asm_data->normval1 * asm_data->normval4);
		}
	}

/* For x87 and SSE2 FFTs:  if the carry must be spread over more than 2 words, then set */
/* variable so that assembly code knows this.  We check to see if three small FFT words */
/* can absorb the expected number of bits in a result word.  We are not */
/* aggressive in pushing this limit (we assume no big words will absorb */
/* any of the carry) as it is not a major performance penalty to do 4 or 6 word */
/* carry propagations.  In fact, we might should do 4 or 6 words all the time. */

	if (!gwdata->information_only) {
	    if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
		if (gwdata->ZERO_PADDED_FFT ||
		    3.0 * gwdata->NUM_B_PER_SMALL_WORD * log2 (b) >
				2.0 * ((gwdata->NUM_B_PER_SMALL_WORD + 1) * log2 (b) - 1) +
				0.6 * log2 (gwdata->FFTLEN) + log2 (k) + 1.7 * log2 (labs (c)))
			asm_data->SPREAD_CARRY_OVER_EXTRA_WORDS = FALSE;
		else
			asm_data->SPREAD_CARRY_OVER_EXTRA_WORDS = TRUE;
	    }

/* Set some global variables that make life easier in the assembly code */
/* that wraps carry out of top FFT word into the bottom FFT word. */
/* This is needed when k > 1 and we are not doing a zero padded FFT. */

	    asm_data->TOP_CARRY_NEEDS_ADJUSTING = (k > 1.0 && !gwdata->ZERO_PADDED_FFT);
	    if (asm_data->TOP_CARRY_NEEDS_ADJUSTING) {
		unsigned long num_b_in_top_word, num_b_in_second_top_word, num_b_in_third_top_word, num_b_in_k;
		double	carry_adjust_1_mod_k;

/* Copy k and inverted k */

		asm_data->K = k;
		asm_data->INVERSE_K = 1.0 / k;

/* Calculate top carry adjusting constants */

		num_b_in_top_word = gwdata->NUM_B_PER_SMALL_WORD;
		if (is_big_word (gwdata, gwdata->FFTLEN-1)) num_b_in_top_word++;
		num_b_in_second_top_word = gwdata->NUM_B_PER_SMALL_WORD;
		if (is_big_word (gwdata, gwdata->FFTLEN-2)) num_b_in_second_top_word++;
		num_b_in_third_top_word = gwdata->NUM_B_PER_SMALL_WORD;
		if (is_big_word (gwdata, gwdata->FFTLEN-3)) num_b_in_third_top_word++;

		num_b_in_k = (unsigned long) ceil (log (k) / log (b));
		asm_data->CARRY_ADJUST1 = pow ((double) b, num_b_in_k);
		carry_adjust_1_mod_k = fltmod (asm_data->CARRY_ADJUST1, k);
		asm_data->CARRY_ADJUST1_HI = floor (carry_adjust_1_mod_k / 131072.0);
		asm_data->CARRY_ADJUST1_LO = carry_adjust_1_mod_k - asm_data->CARRY_ADJUST1_HI * 131072.0;
		asm_data->TWO_TO_17 = 131072.0;
		asm_data->CARRY_ADJUST2 = pow ((double) b, num_b_in_top_word) / asm_data->CARRY_ADJUST1;
		asm_data->CARRY_ADJUST4 = pow ((double) b, num_b_in_second_top_word);
		asm_data->CARRY_ADJUST6 = pow ((double) b, num_b_in_third_top_word);
		if (! (gwdata->cpu_flags & CPU_AVX512F)) {
			if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
				asm_data->CARRY_ADJUST3 = gwfft_partial_weight (gwdata->dd_data, gwdata->FFTLEN-1, dwpn_col (gwdata, gwdata->FFTLEN-1));
				asm_data->CARRY_ADJUST5 = gwfft_partial_weight (gwdata->dd_data, gwdata->FFTLEN-2, dwpn_col (gwdata, gwdata->FFTLEN-2));
				asm_data->CARRY_ADJUST7 = gwfft_partial_weight (gwdata->dd_data, gwdata->FFTLEN-3, dwpn_col (gwdata, gwdata->FFTLEN-3));
			} else {
				asm_data->CARRY_ADJUST3 = gwfft_weight (gwdata->dd_data, gwdata->FFTLEN-1);
				asm_data->CARRY_ADJUST5 = gwfft_weight (gwdata->dd_data, gwdata->FFTLEN-2);
				asm_data->CARRY_ADJUST7 = gwfft_weight (gwdata->dd_data, gwdata->FFTLEN-3);
			}
		}

		// It's not worth our time to upgrade the old x87 code to match the AVX-512/AVX/SSE2 code.
		// So, for x87 generate the same constants used prior to version 25.11
		if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2))) {
			unsigned long kbits, kbits_lo;
			kbits = (unsigned long) ceil (log2 (k));
			kbits_lo = kbits / 2;
			asm_data->u.x87.ALT_K_HI = ((unsigned long) k) & ~((1 << kbits_lo) - 1);
			asm_data->u.x87.ALT_K_LO = ((unsigned long) k) &  ((1 << kbits_lo) - 1);
			asm_data->CARRY_ADJUST1_HI = ((unsigned long) asm_data->CARRY_ADJUST1) & ~((1 << kbits_lo) - 1);
			asm_data->CARRY_ADJUST1_LO = ((unsigned long) asm_data->CARRY_ADJUST1) &  ((1 << kbits_lo) - 1);
			if (gwdata->PASS2_SIZE)
				asm_data->CARRY_ADJUST4 *= asm_data->CARRY_ADJUST5;
			else
				asm_data->CARRY_ADJUST6 *= asm_data->CARRY_ADJUST7;
		}

/* In two-pass FFTs, we only support tweaking the top two words. In one-pass FFTs, */
/* we adjust the top three words.  Make sure this works.  A test case that fails: */
/* 489539*3^72778+1.  We should consider supporting tweaking the top 3 words. */

		ASSERTG ((gwdata->PASS2_SIZE &&
			  num_b_in_k <= num_b_in_top_word + num_b_in_second_top_word) ||
			 (!gwdata->PASS2_SIZE &&
			  num_b_in_k <= num_b_in_top_word + num_b_in_second_top_word + num_b_in_third_top_word));
		if (!	((gwdata->PASS2_SIZE &&
			  num_b_in_k <= num_b_in_top_word + num_b_in_second_top_word) ||
			 (!gwdata->PASS2_SIZE &&
			  num_b_in_k <= num_b_in_top_word + num_b_in_second_top_word + num_b_in_third_top_word))) return (GWERROR_INTERNAL + 8);

/* Get the addr of the top three words.  This is funky because in two-pass */
/* FFTs we want the scratch area offset when normalizing after a multiply, */
/* but the FFT data when normalizing after an add/sub.  For one-pass FFTs, */
/* we always want the FFT data offset. */

		asm_data->HIGH_WORD1_OFFSET = addr_offset (gwdata, gwdata->FFTLEN-1);
		asm_data->HIGH_WORD2_OFFSET = addr_offset (gwdata, gwdata->FFTLEN-2);
		asm_data->HIGH_WORD3_OFFSET = addr_offset (gwdata, gwdata->FFTLEN-3);

		raw_gwsetaddin (gwdata, gwdata->FFTLEN-1, NULL, 0.0);
		asm_data->HIGH_SCRATCH1_OFFSET = asm_data->ADDIN_OFFSET;
		raw_gwsetaddin (gwdata, gwdata->FFTLEN-2, NULL, 0.0);
		asm_data->HIGH_SCRATCH2_OFFSET = asm_data->ADDIN_OFFSET;
		raw_gwsetaddin (gwdata, gwdata->FFTLEN-3, NULL, 0.0);
		asm_data->HIGH_SCRATCH3_OFFSET = asm_data->ADDIN_OFFSET;
	    }

/* Set some global variables that make life easier in the assembly code that handles zero padded FFTs. */

	    if (gwdata->ZERO_PADDED_FFT) {
		unsigned long num_b_in_k, num_b_in_word_0, num_b_in_word_1;
		unsigned long num_b_in_word_2, num_b_in_word_3, num_b_in_word_4, num_b_in_word_5;

		// Compute constant used for zero-padded ADDIN_VALUE when k=1 and c!=1
		// when c = -1, 1 = b^n
		// when c = 1, -1 = b^n, 1 = -b^n
		// when c = -3, 3 = b^n, 1 = b^n - 2
		// when c = 3, -3 = b^n, 3 = -b^n, 1 = -b^n - 2
		// Pre-calculate the "- 2".  For c > 0, negate the result to compensate for the negation of ADDIN_VALUE in gwsetaddin.
		asm_data->ZPAD_LSW_ADJUST = (double) (c < 0 ? c + 1 : c - 1);

		if (gwdata->cpu_flags & CPU_SSE2 && ! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
			asm_data->u.xmm.ZPAD_WORD5_OFFSET = addr_offset (gwdata, 4);
			if (asm_data->u.xmm.ZPAD_WORD5_OFFSET == 8) {  /* FFTLEN = 80 and 112 */
				asm_data->u.xmm.ZPAD_WORD5_RBP_OFFSET = 8;
			} else {
				asm_data->u.xmm.ZPAD_WORD5_RBP_OFFSET = 256;
			}
		}

		asm_data->HIGH_WORD1_OFFSET = addr_offset (gwdata, gwdata->FFTLEN/2-1);
		asm_data->HIGH_WORD2_OFFSET = addr_offset (gwdata, gwdata->FFTLEN/2-2);
		asm_data->HIGH_WORD3_OFFSET = addr_offset (gwdata, gwdata->FFTLEN/2-3);

		raw_gwsetaddin (gwdata, gwdata->FFTLEN/2-1, NULL, 0.0);
		asm_data->HIGH_SCRATCH1_OFFSET = asm_data->ADDIN_OFFSET;
		raw_gwsetaddin (gwdata, gwdata->FFTLEN/2-2, NULL, 0.0);
		asm_data->HIGH_SCRATCH2_OFFSET = asm_data->ADDIN_OFFSET;
		raw_gwsetaddin (gwdata, gwdata->FFTLEN/2-3, NULL, 0.0);
		asm_data->HIGH_SCRATCH3_OFFSET = asm_data->ADDIN_OFFSET;

		num_b_in_k = (unsigned long) ceil (log (k) / log (b));
		num_b_in_word_0 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 0)) num_b_in_word_0++;
		num_b_in_word_1 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 1)) num_b_in_word_1++;
		num_b_in_word_2 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 2)) num_b_in_word_2++;
		num_b_in_word_3 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 3)) num_b_in_word_3++;
		num_b_in_word_4 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 4)) num_b_in_word_4++;
		num_b_in_word_5 = gwdata->NUM_B_PER_SMALL_WORD; if (is_big_word (gwdata, 5)) num_b_in_word_5++;

		asm_data->ZPAD_SHIFT1 = pow ((double) b, (int) num_b_in_word_0);
		asm_data->ZPAD_SHIFT2 = pow ((double) b, (int) num_b_in_word_1);
		asm_data->ZPAD_SHIFT3 = pow ((double) b, (int) num_b_in_word_2);
		asm_data->ZPAD_SHIFT4 = pow ((double) b, (int) num_b_in_word_3);
		asm_data->ZPAD_SHIFT5 = pow ((double) b, (int) num_b_in_word_4);
		asm_data->ZPAD_SHIFT6 = pow ((double) b, (int) num_b_in_word_5);

		if (num_b_in_k <= num_b_in_word_0) asm_data->ZPAD_TYPE = 1;
		else if (num_b_in_k <= num_b_in_word_0 + num_b_in_word_1) asm_data->ZPAD_TYPE = 2;
		else asm_data->ZPAD_TYPE = 3;

		if (asm_data->ZPAD_TYPE == 1) {
			asm_data->ZPAD_K1_LO = k;
			asm_data->ZPAD_INVERSE_K1 = 1.0 / k;
		}

		if (asm_data->ZPAD_TYPE == 2) {
			asm_data->ZPAD_K1_HI = floor (k / asm_data->ZPAD_SHIFT1);
			asm_data->ZPAD_K1_LO = k - asm_data->ZPAD_K1_HI * asm_data->ZPAD_SHIFT1;
			asm_data->ZPAD_INVERSE_K1 = asm_data->ZPAD_SHIFT1 / k;
			asm_data->ZPAD_K2_HI = floor (k / asm_data->ZPAD_SHIFT2);
			asm_data->ZPAD_K2_LO = k - asm_data->ZPAD_K2_HI * asm_data->ZPAD_SHIFT2;
			asm_data->ZPAD_INVERSE_K2 = asm_data->ZPAD_SHIFT2 / k;
			asm_data->ZPAD_K3_HI = floor (k / asm_data->ZPAD_SHIFT3);
			asm_data->ZPAD_K3_LO = k - asm_data->ZPAD_K3_HI * asm_data->ZPAD_SHIFT3;
			asm_data->ZPAD_INVERSE_K3 = asm_data->ZPAD_SHIFT3 / k;
			asm_data->ZPAD_K4_HI = floor (k / asm_data->ZPAD_SHIFT4);
			asm_data->ZPAD_K4_LO = k - asm_data->ZPAD_K4_HI * asm_data->ZPAD_SHIFT4;
			asm_data->ZPAD_INVERSE_K4 = asm_data->ZPAD_SHIFT4 / k;
			asm_data->ZPAD_K5_HI = floor (k / asm_data->ZPAD_SHIFT5);
			asm_data->ZPAD_K5_LO = k - asm_data->ZPAD_K5_HI * asm_data->ZPAD_SHIFT5;
			asm_data->ZPAD_INVERSE_K5 = asm_data->ZPAD_SHIFT5 / k;
			asm_data->ZPAD_K6_HI = floor (k / asm_data->ZPAD_SHIFT6);
			asm_data->ZPAD_K6_LO = k - asm_data->ZPAD_K6_HI * asm_data->ZPAD_SHIFT6;
			asm_data->ZPAD_INVERSE_K6 = asm_data->ZPAD_SHIFT6 / k;
		}

		if (asm_data->ZPAD_TYPE == 3) {
			double	powb, bigpowb;
			powb = pow ((double) b, (int) num_b_in_word_0);
			bigpowb = pow ((double) b, (int) (num_b_in_word_0 + num_b_in_word_1));
			asm_data->ZPAD_K2_HI = floor (k / bigpowb);
			asm_data->ZPAD_K2_MID = floor ((k - asm_data->ZPAD_K2_HI*bigpowb) / powb);
			asm_data->ZPAD_K2_LO = k - asm_data->ZPAD_K2_HI*bigpowb - asm_data->ZPAD_K2_MID*powb;
			asm_data->ZPAD_INVERSE_K2 = powb / k;
			powb = pow ((double) b, (int) num_b_in_word_1);
			bigpowb = pow ((double) b, (int) (num_b_in_word_1 + num_b_in_word_2));
			asm_data->ZPAD_K3_HI = floor (k / bigpowb);
			asm_data->ZPAD_K3_MID = floor ((k - asm_data->ZPAD_K3_HI*bigpowb) / powb);
			asm_data->ZPAD_K3_LO = k - asm_data->ZPAD_K3_HI*bigpowb - asm_data->ZPAD_K3_MID*powb;
			asm_data->ZPAD_INVERSE_K3 = powb / k;
			powb = pow ((double) b, (int) num_b_in_word_2);
			bigpowb = pow ((double) b, (int) (num_b_in_word_2 + num_b_in_word_3));
			asm_data->ZPAD_K4_HI = floor (k / bigpowb);
			asm_data->ZPAD_K4_MID = floor ((k - asm_data->ZPAD_K4_HI*bigpowb) / powb);
			asm_data->ZPAD_K4_LO = k - asm_data->ZPAD_K4_HI*bigpowb - asm_data->ZPAD_K4_MID*powb;
			asm_data->ZPAD_INVERSE_K4 = powb / k;
			powb = pow ((double) b, (int) num_b_in_word_3);
			bigpowb = pow ((double) b, (int) (num_b_in_word_3 + num_b_in_word_4));
			asm_data->ZPAD_K5_HI = floor (k / bigpowb);
			asm_data->ZPAD_K5_MID = floor ((k - asm_data->ZPAD_K5_HI*bigpowb) / powb);
			asm_data->ZPAD_K5_LO = k - asm_data->ZPAD_K5_HI*bigpowb - asm_data->ZPAD_K5_MID*powb;
			asm_data->ZPAD_INVERSE_K5 = powb / k;
			powb = pow ((double) b, (int) num_b_in_word_4);
			bigpowb = pow ((double) b, (int) (num_b_in_word_4 + num_b_in_word_5));
			asm_data->ZPAD_K6_HI = floor (k / bigpowb);
			asm_data->ZPAD_K6_MID = floor ((k - asm_data->ZPAD_K6_HI*bigpowb) / powb);
			asm_data->ZPAD_K6_LO = k - asm_data->ZPAD_K6_HI*bigpowb - asm_data->ZPAD_K6_MID*powb;
			asm_data->ZPAD_INVERSE_K6 = bigpowb / k;
		}

/* Pre-compute the adjustments to copying the 7 words around the halfway point */
/* In a radix-4 delay with full or partial normalization, we must apply an adjustment */
/* so that the copied words are fully weighted. */

		if (gwdata->cpu_flags & CPU_AVX512F) {
			int	i;
			for (i = 0; i < 7; i++) {
				/* Weights for words we are copying before an FFT is performed */
				gwdata->ZPAD_COPY7_ADJUST[i] = gwfft_weight (gwdata->dd_data, gwdata->FFTLEN / 2 - 3 + i);
				/* Inverse weights for ZPAD0 - ZPAD6 applied after an inverse FFT is performed */
				gwdata->ZPAD_0_6_ADJUST[i] = gwfft_weight_inverse (gwdata->dd_data, i);
			}
		} else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
			int	i;
			for (i = 0; i < 7; i++) {
				/* Partial weights for words we are copying before an FFT is performed */
				gwdata->ZPAD_COPY7_ADJUST[i] = gwfft_weight (gwdata->dd_data, dwpn_col (gwdata, gwdata->FFTLEN / 2 - 3 + i));
				/* Partial inverse weights for ZPAD0 - ZPAD6 applied after an inverse FFT is performed */
				gwdata->ZPAD_0_6_ADJUST[i] = gwfft_weight_inverse (gwdata->dd_data, dwpn_col (gwdata, i));
			}
		} else {
			int	i;
			for (i = 0; i < 7; i++) gwdata->ZPAD_COPY7_ADJUST[i] = 1.0;
		}
	    }

/* Set the procedure pointers from the proc tables */

#ifdef X86_64
	    if (gwdata->cpu_flags & CPU_AVX512F) {
		int	index;
		index = 0;
		if (gwdata->ZERO_PADDED_FFT) index += 2;
		if (gwdata->RATIONAL_FFT) index += 1;
		asm_data->u.zmm.ZMM_CARRIES_ROUTINE = avx512_carries_prctab[index]; // Two-pass square/multiply carry propagation routine
		asm_data->u.zmm.ZMM_OP_CARRIES_ROUTINE = avx512_carries_prctab[gwdata->ZERO_PADDED_FFT ? index + 2 : index]; // Two-pass add/sub/smallmul carry propagation routine
		index = 4 + 4 * index;
		gwdata->GWPROCPTRS[1] = avx512_aux_prctab[index];	// Add
		gwdata->GWPROCPTRS[2] = avx512_aux_prctab[0];		// Add quick
		gwdata->GWPROCPTRS[3] = avx512_aux_prctab[index+1];	// Subtract
		gwdata->GWPROCPTRS[4] = avx512_aux_prctab[1];		// Sub quick
		gwdata->GWPROCPTRS[5] = avx512_aux_prctab[index+2];	// AddSub
		gwdata->GWPROCPTRS[6] = avx512_aux_prctab[2];		// AddSub quick
		gwdata->GWPROCPTRS[7] = avx512_aux_prctab[3];		// Copyzero
		gwdata->GWPROCPTRS[8] = avx512_aux_prctab[index+3];	// Mul small
		gwdata->GWPROCPTRS[norm_routines] = avx512_prctab[avx512_prctab_index (gwdata, 0, 0, 0)];  // No error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+1] = avx512_prctab[avx512_prctab_index (gwdata, 0, 1, 0)];  // Error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+2] = avx512_prctab[avx512_prctab_index (gwdata, 0, 0, 1)];  // No error, mulbyconst
		gwdata->GWPROCPTRS[norm_routines+3] = avx512_prctab[avx512_prctab_index (gwdata, 0, 1, 1)];  // Error, mulbyconst
		gwdata->GWPROCPTRS[zerohigh_routines] = avx512_prctab[avx512_prctab_index (gwdata, 1, 0, 0)];  // Zero, no error
		gwdata->GWPROCPTRS[zerohigh_routines+1] = avx512_prctab[avx512_prctab_index (gwdata, 1, 1, 0)];  // Zero, error
	    } else
#endif
	    if (gwdata->cpu_flags & CPU_AVX) {
		int	index, aux_base;
		index = 0;
		if (b != 2) index += 4;
		if (gwdata->ZERO_PADDED_FFT) index += 2;
		if (gwdata->RATIONAL_FFT) index += 1;
		asm_data->u.ymm.YMM_CARRIES_ROUTINE = avx_carries_prctab[index]; // Two-pass carry propagation routine
		aux_base = (gwdata->PASS2_SIZE == 0) ? 0 : 36;
		index = aux_base + 4 + 4 * index;
		gwdata->GWPROCPTRS[1] = avx_aux_prctab[index];		// Add
		gwdata->GWPROCPTRS[2] = avx_aux_prctab[aux_base];	// Add quick
		gwdata->GWPROCPTRS[3] = avx_aux_prctab[index+1];	// Subtract
		gwdata->GWPROCPTRS[4] = avx_aux_prctab[aux_base+1];	// Sub quick
		gwdata->GWPROCPTRS[5] = avx_aux_prctab[index+2];	// AddSub
		gwdata->GWPROCPTRS[6] = avx_aux_prctab[aux_base+2];	// AddSub quick
		gwdata->GWPROCPTRS[7] = avx_aux_prctab[aux_base+3];	// Copyzero
		gwdata->GWPROCPTRS[8] = avx_aux_prctab[index+3];	// Mul small
		gwdata->GWPROCPTRS[norm_routines] = avx_prctab[avx_prctab_index (gwdata, 0, 0, 0)];  // No error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+1] = avx_prctab[avx_prctab_index (gwdata, 0, 1, 0)];  // Error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+2] = avx_prctab[avx_prctab_index (gwdata, 0, 0, 1)];  // No error, mulbyconst
		gwdata->GWPROCPTRS[norm_routines+3] = avx_prctab[avx_prctab_index (gwdata, 0, 1, 1)];  // Error, mulbyconst
		gwdata->GWPROCPTRS[zerohigh_routines] = avx_prctab[avx_prctab_index (gwdata, 1, 0, 0)];  // Zero, no error
		gwdata->GWPROCPTRS[zerohigh_routines+1] = avx_prctab[avx_prctab_index (gwdata, 1, 1, 0)];  // Zero, error
	    }
	    else if (gwdata->cpu_flags & CPU_SSE2) {
		memcpy (gwdata->GWPROCPTRS+1, &sse2_aux_prctab[gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN ? 16 : gwdata->PASS2_SIZE ? 8 : 0], 8 * sizeof (void *));
		gwdata->GWPROCPTRS[norm_routines] = sse2_prctab[sse2_prctab_index (gwdata, 0, 0, 0)];  // No error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+1] = sse2_prctab[sse2_prctab_index (gwdata, 0, 1, 0)];  // Error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+2] = sse2_prctab[sse2_prctab_index (gwdata, 0, 0, 1)];  // No error, mulbyconst
		gwdata->GWPROCPTRS[norm_routines+3] = sse2_prctab[sse2_prctab_index (gwdata, 0, 1, 1)];  // Error, mulbyconst
		gwdata->GWPROCPTRS[zerohigh_routines] = sse2_prctab[sse2_prctab_index (gwdata, 1, 0, 0)];  // Zero, no error
		gwdata->GWPROCPTRS[zerohigh_routines+1] = sse2_prctab[sse2_prctab_index (gwdata, 1, 1, 0)];  // Zero, error
	    }
#ifndef X86_64
	    else {
		memcpy (gwdata->GWPROCPTRS+1, &x87_aux_prctab[gwdata->PASS2_SIZE ? 8 : 0], 8 * sizeof (void *));
		gwdata->GWPROCPTRS[norm_routines] = x87_prctab[x87_prctab_index (gwdata, 0, 0, 0)];  // No error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+1] = x87_prctab[x87_prctab_index (gwdata, 0, 1, 0)];  // Error, no mulbyconst
		gwdata->GWPROCPTRS[norm_routines+2] = x87_prctab[x87_prctab_index (gwdata, 0, 0, 1)];  // No error, mulbyconst
		gwdata->GWPROCPTRS[norm_routines+3] = x87_prctab[x87_prctab_index (gwdata, 0, 1, 1)];  // Error, mulbyconst
		gwdata->GWPROCPTRS[zerohigh_routines] = x87_prctab[x87_prctab_index (gwdata, 1, 0, 0)];  // Zero, no error
		gwdata->GWPROCPTRS[zerohigh_routines+1] = x87_prctab[x87_prctab_index (gwdata, 1, 1, 0)];  // Zero, error
	    }
#endif

/* Default normalization routines and behaviors */

	    gwsetnormroutine (gwdata, 0, 0, 0);
	    gwstartnextfft (gwdata, 0);
	    gwdata->asm_addin_value = 0.0;
	    gwdata->asm_postaddin_value = 0.0;
	    if (gwdata->careful_count == -1) gwset_carefully_count (gwdata, -1);
	}

/* Clear globals */

	asm_data->MAXERR = 0.0;
	gwdata->saved_copyz_n = 0;
	gwdata->GWERROR = 0;
	gwdata->GW_RANDOM = NULL;
	gwdata->GW_RANDOM_SQUARED = NULL;
	gwdata->GW_RANDOM_FFT = NULL;
	gwdata->GW_ADDIN = NULL;
	gwdata->GW_POSTADDIN = NULL;

/* Init FFT1 state for possible future gwmuladd4, gwmulsub4, and gwunfft2 calls */
/* For AVX-512, FMA3 and AVX FFTs, k == 1 FFTs can skip FFT(1) */
/* For 32-bit gwnum, we'll never need FFT(1) we emulate everywhere it might be needed */

	gwdata->GW_FFT1 = NULL;
	gwdata->FFT1_state = 0;
	gwdata->FFT1_user_allocated = 0;
#ifdef X86_64
	if (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX)) { if (gwdata->k == 1.0 || gwdata->GENERAL_MMGW_MOD) gwdata->FFT1_state = 2; }
#else
	gwdata->FFT1_state = 2;
#endif

// Inits needed for GENERAL_MMGW_MOD's sub-gwdatas

	gwdata->cyclic_gwdata = NULL;
	gwdata->negacyclic_gwdata = NULL;

/* Clear counters, init internal timers */

	if (!gwdata->information_only) {
		atomic_set (gwdata->clone_count, 0);
		gwdata->fft_count = 0;
		gwdata->read_count = gwdata->write_count = 0;
		asm_data->ASM_TIMERS = (uint32_t *) &gwdata->ASM_TIMERS;

/* Default size of gwnum_alloc array is 50.  The caller might clone this gwdata.  If so, gwalloc and gwfree need a lock to operate in a thread-safe manner. */

		gwdata->gwnum_alloc = (gwnum *) malloc (50 * sizeof (gwnum));
		if (gwdata->gwnum_alloc == NULL) { gwdone (gwdata); return (GWERROR_MALLOC); }
		gwdata->gwnum_free = (gwnum *) malloc (50 * sizeof (gwnum));
		if (gwdata->gwnum_free == NULL) { gwdone (gwdata); return (GWERROR_MALLOC); }
		gwdata->gwnum_alloc_count = 0;
		gwdata->gwnum_alloc_array_size = 50;
		gwdata->gwnum_free_count = 0;
		gwmutex_init (&gwdata->alloc_lock);

		// Allocate and free the special large pages gwnum before any clones can be made.  This will put the special gwnum in the free cache
		// and simplify locking in gwalloc (it won't have to obtain a lock to test gwdata->large_pages_gwnum).
		if (gwdata->large_pages_gwnum != NULL) gwfree (gwdata, gwalloc (gwdata));

		// Activate giants / gwnum shared cached memory allocates
		gwdata->gdata.blksize = gwnum_datasize (gwdata);

/* If we are going to use multiple threads for multiplications, then do the required multi-thread initializations. */
/* Someday, we might allow setting num_threads after gwsetup so we put all the multi-thread initialization in its own routine. */

		int retcode = multithread_init (gwdata);
		if (retcode) { gwdone (gwdata); return (retcode); }
	}

/* Test new gwiter routines */

#ifdef GDEBUG
	if (!gwdata->information_only) {
	gwiter iter;
	gwnum g = gwalloc (gwdata);
	int32_t val, val2;
	gwiter_init_write_only (gwdata, &iter, g);
	for (unsigned long i = 0; i < gwdata->FFTLEN; i++, gwiter_next (&iter)) {
		ASSERTG (gwiter_index (&iter) == i);
		ASSERTG (gwiter_addr_offset (&iter) == addr_offset (gwdata, i));
		ASSERTG (gwiter_addr (&iter) == addr (gwdata, g, i));
		ASSERTG (gwiter_is_big_word (&iter) == is_big_word (gwdata, i));
		set_fft_value (gwdata, g, i, 987654321);
		gwiter_get_fft_value (&iter, &val);
		ASSERTG (val == 987654321);
		gwiter_set_fft_value (&iter, -123456789);
		gwiter_get_fft_value (&iter, &val2);
		ASSERTG (val2 == -123456789);
	}
	unsigned long rand_start = ((rand () << 16) + rand ()) % gwdata->FFTLEN;
	gwiter_init (gwdata, &iter, g, rand_start);
	for (unsigned long i = rand_start; i < gwdata->FFTLEN; i++, gwiter_next (&iter)) {
		ASSERTG (gwiter_index (&iter) == i);
		ASSERTG (gwiter_addr_offset (&iter) == addr_offset (gwdata, i));
		ASSERTG (gwiter_addr (&iter) == addr (gwdata, g, i));
		ASSERTG (gwiter_is_big_word (&iter) == is_big_word (gwdata, i));
		set_fft_value (gwdata, g, i, 897654321);
		gwiter_get_fft_value (&iter, &val);
		ASSERTG (val == 897654321);
		gwiter_set_fft_value (&iter, -213456789);
		gwiter_get_fft_value (&iter, &val2);
		ASSERTG (val2 == -213456789);
	}
	gwfree (gwdata, g);
	}
#endif

/* Success */

	return (0);
}


/* Utility routines to deal with the 7 words near the half-way point */
/* in a zero-padded AVX-512/AVX/SSE2 FFT.  This used to be all done in assembly code, */
/* but I moved it to C code when the multithread code was written. */

/* When doing zero-padded FFTs, the 7 words around the halfway point must be copied (after applying weights) */
/* for later processing.  This macro does that before a typical forward FFT. */

void xcopy_7_words (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata;
	double	*srcarg, *destarg;

	gwdata = asm_data->gwdata;
	destarg = (double *) asm_data->DESTARG;
	srcarg = (double *) ((char *) destarg + asm_data->DIST_TO_FFTSRCARG);
	if (gwdata->cpu_flags & CPU_AVX512F) {
		char	*srcp;
		srcp = (char *) srcarg + 64;
		destarg[-8] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[3];	/* Copy 1st word above halfway point */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		destarg[-7] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		destarg[-6] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		destarg[-5] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	}
	else if (gwdata->cpu_flags & CPU_AVX) {
		char	*srcp;
		srcp = (char *) srcarg + 32;
		destarg[-8] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[3];	/* Copy 1st word above halfway point */
		srcp += asm_data->u.ymm.YMM_SRC_INCR1;
		destarg[-7] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		srcp += asm_data->u.ymm.YMM_SRC_INCR2;
		destarg[-6] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		srcp += asm_data->u.ymm.YMM_SRC_INCR3;
		destarg[-5] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	} else {
		destarg[-8] = srcarg[4] * gwdata->ZPAD_COPY7_ADJUST[3];	/* Copy 1st word above halfway point */
		destarg[-7] = srcarg[12] * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		destarg[-6] = srcarg[20] * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		destarg[-5] = srcarg[28] * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	}
	destarg[-9] = * (double *)  ((char *) srcarg + asm_data->HIGH_WORD1_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[2];	/* Copy 1st word below halfway point */
	destarg[-10] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD2_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[1];	/* Copy 2nd word below */
	destarg[-11] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD3_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[0];	/* Copy 3rd word below */
}

/* When POSTFFT is set, we must copy the 7 words at two different spots. */
/* These two routines copy the four values above the half-way point after */
/* carries have been propagated and copy the three words just below the */
/* half-way point right after the last NORMRTN has been called. */

void xcopy_4_words (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata;
	double	*srcarg;

	gwdata = asm_data->gwdata;
	srcarg = (double *) asm_data->DESTARG;
	if (gwdata->cpu_flags & CPU_AVX512F) {
		char	*srcp;
		srcp = (char *) srcarg + 64;
		srcarg[-8] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[3];	/* Copy 1st word above halfway point */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		srcarg[-7] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		srcarg[-6] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		srcp += asm_data->u.zmm.ZMM_SRC_INCR;
		srcarg[-5] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	}
	else if (gwdata->cpu_flags & CPU_AVX) {
		char	*srcp;
		srcp = (char *) srcarg + 32;
		srcarg[-8] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[3];	/* Copy 1st word above halfway point */
		srcp += asm_data->u.ymm.YMM_SRC_INCR1;
		srcarg[-7] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		srcp += asm_data->u.ymm.YMM_SRC_INCR2;
		srcarg[-6] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		srcp += asm_data->u.ymm.YMM_SRC_INCR3;
		srcarg[-5] = * (double *) srcp * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	} else {
		srcarg[-8] = srcarg[4] * gwdata->ZPAD_COPY7_ADJUST[3]; /* Copy 1st word above halfway point */
		srcarg[-7] = srcarg[12] * gwdata->ZPAD_COPY7_ADJUST[4]; /* Copy 2nd word */
		srcarg[-6] = srcarg[20] * gwdata->ZPAD_COPY7_ADJUST[5]; /* Copy 3rd word */
		srcarg[-5] = srcarg[28] * gwdata->ZPAD_COPY7_ADJUST[6]; /* Copy 4th word */
	}
}

void xcopy_3_words (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata;
	double	*srcarg;

/* Get pointer to the gwdata structure */

	gwdata = asm_data->gwdata;

/* Copy 1st and 2nd words below the halfway point from the last block */

	srcarg = (double *) asm_data->DESTARG;
	if (asm_data->this_block == asm_data->last_pass1_block) {
		if (gwdata->SCRATCH_SIZE) {
			srcarg[-9] = * (double *)  ((char *) asm_data->scratch_area + asm_data->HIGH_SCRATCH1_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[2];
			srcarg[-10] = * (double *) ((char *) asm_data->scratch_area + asm_data->HIGH_SCRATCH2_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[1];
		} else {
			srcarg[-9] = * (double *)  ((char *) srcarg + asm_data->HIGH_WORD1_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[2];
			srcarg[-10] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD2_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[1];
		}
	}

/* Copy 3rd word below the halfway point */

	if (asm_data->this_block <= gwdata->num_pass1_blocks - 3 &&
	    asm_data->this_block + asm_data->cache_line_multiplier > gwdata->num_pass1_blocks - 3) {
		if (gwdata->SCRATCH_SIZE) {
			srcarg[-11] = * (double *) ((char *) asm_data->scratch_area + asm_data->HIGH_SCRATCH3_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[0];
		} else {
			srcarg[-11] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD3_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[0];
		}
	}
}

void xcopy_3_words_after_gwcarries (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata;
	double	*srcarg;

/* Get pointer to the gwdata structure */

	gwdata = asm_data->gwdata;

/* Copy three words below the halfway point from the FFT data */

	srcarg = (double *) asm_data->DESTARG;
	srcarg[-9] = * (double *)  ((char *) srcarg + asm_data->HIGH_WORD1_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[2];
	srcarg[-10] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD2_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[1];
	srcarg[-11] = * (double *) ((char *) srcarg + asm_data->HIGH_WORD3_OFFSET) * gwdata->ZPAD_COPY7_ADJUST[0];
}

/* Possible states for the (mis-named) gwdata->pass1_state variable */
/* 0 = pass 1 forward fft */
/* 1 = pass 1 inverse fft */
#define	PASS1_STATE_PASS2		999		/* Auxiliary thread is doing pass 2 work */
#define	PASS1_STATE_MULTITHREAD_OP	4000		/* Auxiliary thread is doing add/sub/addsub/smallmul work */

/* Inline routines for the processing blocks in multi-thread code below */

/* Calculate pass 1 block address.  Note that SSE2 block addresses are */
/* computed based on 128 pad bytes after every 128 cache lines.  That is: */
/* DESTARG + (block * 64) + (block >> 7 * 128).  AVX-512/AVX addresses are based */
/* on 64 to 192 pad bytes after every 64 cache lines. */

static __inline void *pass1_data_addr (
	gwhandle *gwdata,
	struct gwasm_data *asm_data,
	unsigned long block)
{
	if (gwdata->cpu_flags & CPU_AVX512F) {
		block >>= 3;
		return ((char *) asm_data->DESTARG + (block << 7) + ((block >> 5) * gwdata->FOURKBGAPSIZE));
	} else if (gwdata->cpu_flags & CPU_AVX) {
		block >>= 2;
		return ((char *) asm_data->DESTARG + (block << 6) + ((block >> 6) * gwdata->FOURKBGAPSIZE));
	} else
		return ((char *) asm_data->DESTARG + (block << 6) + ((block >> 7) << 7));
}

/* Calculate pass 1 sin/cos/premult address (for those FFTs that do not use */
/* the same sin/cos table for every pass 1 group). */

static __inline void *pass1_premult_addr (
	gwhandle *gwdata,
	unsigned long block)
{
	if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4 ||
	    gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DELAYED ||
	    gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
		return ((char *) gwdata->pass1_var_data + block / gwdata->PASS1_CACHE_LINES * gwdata->pass1_var_data_size);
	return (NULL);
}

/* Calculate pass 1 state 1 normalization ptr addresses. */

static __inline void pass1_state1_norm_addrs (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	if (gwdata->cpu_flags & CPU_AVX512F) {
		// AVX-512 puts biglit flags in the variable sin/cos data
	} else if (gwdata->cpu_flags & CPU_AVX) {
		asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 2);
	} else {
		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
			asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 2);
		else {
			asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 4);
			asm_data->norm_ptr2 = (char *) asm_data->norm_col_mults + (asm_data->this_block * 32);
		}
	}
}

static __inline void pass1_state1_carry_addrs (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	if (gwdata->cpu_flags & CPU_AVX512F) {
		// Calculate address of biglit data in this pass 1 block's variable data.
		asm_data->norm_ptr1 = (char *) gwdata->pass1_var_data +
				      gwdata->pass1_var_data_size * asm_data->this_block / gwdata->PASS1_CACHE_LINES + gwdata->biglit_data_offset;
	} else if (gwdata->cpu_flags & CPU_AVX) {
		asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 2);
	} else {
		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
			asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 2);
		else {
			asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + (asm_data->addcount1 * asm_data->this_block * 4);
			asm_data->norm_ptr2 = (char *) asm_data->norm_col_mults + (asm_data->this_block * 32);
		}
	}
}

/* Calculate pass 2 block address */

static __inline void *pass2_data_addr (
	gwhandle *gwdata,
	struct gwasm_data *asm_data,
	unsigned long block)
{
	return ((char *) asm_data->DESTARG + block * (unsigned long) asm_data->pass2blkdst);
}

/* Calculate pass 2 premultiplier address */

static __inline void * pass2_premult_addr (
	gwhandle *gwdata,
	unsigned long block)
{
	return ((char *) gwdata->adjusted_pass2_premults + block * gwdata->pass2_premult_block_size);
}

/* Assign a thread's first block to process in pass 1 state 0.  These are assigned in sequential order. */

static __inline void pass1_state0_assign_first_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	asm_data->this_block = (int) atomic_fetch_addin (gwdata->next_block, asm_data->cache_line_multiplier);
	asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
	asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
}

/* Assign next available block in pass 1 state 0.  These are assigned in sequential order. */

static __inline void pass1_state0_assign_next_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	int next_block = (int) atomic_fetch_addin (gwdata->next_block, asm_data->cache_line_multiplier);
	if (next_block < (int) gwdata->num_pass1_blocks) {
		asm_data->next_block = next_block;
		/* Init prefetching for the next block */
		asm_data->data_prefetch = pass1_data_addr (gwdata, asm_data, asm_data->next_block);
		asm_data->premult_prefetch = pass1_premult_addr (gwdata, asm_data->next_block);
	} else {
		asm_data->next_block = asm_data->this_block;
		asm_data->data_prefetch = asm_data->data_addr;
		asm_data->premult_prefetch = asm_data->premult_addr;
	}
}

/* Acquire lock to allow changing carry section's critical data.  Lock contention should be exceedingly rare. */

void __inline lock_carry_section (gwhandle *gwdata, int i) {
	while (atomic_fetch_incr (gwdata->pass1_carry_sections[i].change_in_progress) != 0) {
		atomic_decr (gwdata->pass1_carry_sections[i].change_in_progress);
		atomic_spinwait (gwdata->pass1_carry_sections[i].change_in_progress, 0);
	}
}

/* Free lock allowing changing of carry section's critical data */

void __inline unlock_carry_section (gwhandle *gwdata, int i) {
	atomic_decr (gwdata->pass1_carry_sections[i].change_in_progress);
}

/* Assign a thread's first block in pass 1 state 1.  Returns TRUE if successful, FALSE if no free sections were found. */

int pass1_state1_assign_first_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	/* Get thread number / section number */
	int i = asm_data->thread_num;

	/* If there are zero blocks in the section, see if we can find (part of) another section we can work on */
	if (gwdata->pass1_carry_sections[i].next_block == gwdata->pass1_carry_sections[i].last_block) {
		unsigned int min_split_size;

		/* Calculate minimum split size.  It must be at least num_postfft_blocks. */
		/* It must also be a multiple of 8 if AVX zero-padded (because of using YMM_SRC_INCR[0-7] in */
		/* ynorm012 macros) or if SSE2 because of using BIGLIT_INCR4 in xnorm012 macros. */
		/* AVX-512 will already be a multiple of 8. */
		min_split_size = gwdata->num_postfft_blocks;
		if (((gwdata->cpu_flags & CPU_AVX) && gwdata->ZERO_PADDED_FFT) || !(gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX)))
			min_split_size = round_up_to_multiple_of (min_split_size, 8);
		/* Since an active section is prefetching the next block, increase the minimum split size so that the active section can processes */
		/* the next block that it is prefetching. */
		min_split_size += asm_data->cache_line_multiplier;
		/* Now splitting a section is not without cost.  Carrying is more efficient without a split.  So, I did some throughput benchmarks on a quad */
		/* core FMA3 machine examining if increasing the minimum split size makes sense.  It did, but conclusions were difficult because noticed */
		/* differences may not have been within the margin of error.  Anyway, it seems increasing the minimum split size one notch is indicated. */
		min_split_size += asm_data->cache_line_multiplier;

		/* Loop until we find a splittable section.  This loop is necessary because we look for the largest splittable section without obtaining */
		/* any locks.  When we finally acquire a lock, the section might no longer be splittable. */
		for ( ; ; ) {
			unsigned int j, size, target, target_size, new_size;

			/* Find largest unfinished section for splitting */
			target_size = 0;
			for (j = 0; j < gwdata->num_threads; j++) {
				/* Only sections in state 1 can be split */
				if (gwdata->pass1_carry_sections[j].section_state != 1) continue;
				/* Calculate number of block not yet processed */
				size = gwdata->pass1_carry_sections[j].last_block - gwdata->pass1_carry_sections[j].next_block;
				/* See if this section is big enough and the largest one thusfar. */
				if (size > target_size && size >= min_split_size) {
					target = j;
					target_size = size;
					// During auxiliary threads startup we divide up the all-encompassing first section.  Skip looking at other sections.
					if (gwdata->pass1_carry_sections_unallocated > 1 && j == 0) break;
				}
			}

			/* If we found no sections that we can split, then return FALSE */
			if (target_size == 0) return (FALSE);

			/* Acquire a lock on the section we are trying to split */
			lock_carry_section (gwdata, target);

			/* Now that we have the target lock, check again that the target section is splittable */
			if (gwdata->pass1_carry_sections[target].section_state == 1)
				target_size = gwdata->pass1_carry_sections[target].last_block - gwdata->pass1_carry_sections[target].next_block;
			else
				target_size = 0;
			if (target_size < min_split_size) {
				unlock_carry_section (gwdata, target);
				continue;
			}

			/* As auxiliary threads start up, limit the number of blocks each takes from the main thread's initial big section. */
			/* Make an extra effort to have each section be about the same size, e.g. 7 blocks among 4 threads as 4,1,1,1 is worse than 1,2,2,2. */
			if (gwdata->pass1_carry_sections_unallocated > 1) {
				/* If all auxiliary threads were to instantly start up, the most even split would be: */
				/* (target_size (blocks not yet processed) + cache_line_multiplier (the block currently being processed)) divided by */
				/* (pass1_carry_sections_unallocated (number of auxiliary threads yet to start) + 1 (the main thread that is being split). */
				/* Round this value up to a multiple of cache_line_multiplier to avoid the 4,1,1,1 vs. 1,2,2,2 situation */
				new_size = divide_rounding_up (target_size + asm_data->cache_line_multiplier, gwdata->pass1_carry_sections_unallocated + 1);
				new_size = round_up_to_multiple_of (new_size, asm_data->cache_line_multiplier);
				gwdata->pass1_carry_sections_unallocated--;
			}

			/* Split the target section in half.  Our goal being to have these two sections finish as close as possible to the same time. */
			/* Since the target section is at least a little bit processed, favor making the amount we're spliting off larger than the amount were */
			/* leaving in the target section.  However, if we're near the end of the target section favor making the amount we're splitting off */
			/* smaller than the amount were leaving in the target section because the target cannot finish its section until the split off section */
			/* has completed its first blocks. */
			else {
				new_size = (target_size + asm_data->cache_line_multiplier) / 2;
				if (new_size > (unsigned int) (4 * asm_data->cache_line_multiplier))
					new_size = round_up_to_multiple_of (new_size, asm_data->cache_line_multiplier);
				else
					new_size = round_down_to_multiple_of (new_size, asm_data->cache_line_multiplier);
			}

			/* Enforce further restrictions on the size of the split off section */
			if (((gwdata->cpu_flags & CPU_AVX512F) && gwdata->ZERO_PADDED_FFT) ||
			    (!(gwdata->cpu_flags & CPU_AVX512F) && (gwdata->cpu_flags & CPU_AVX) && gwdata->ZERO_PADDED_FFT) ||
			    (!(gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))))
				new_size = round_up_to_multiple_of (new_size, 8);
			if (new_size < gwdata->num_postfft_blocks) new_size = gwdata->num_postfft_blocks;

			// Make sure the new size is a multiple of num_postfft_blocks.  This is necessary because
			// a multihreaded PRP test of 66666*5^1560000-1 will fail if we don't.  It fails because
			// we need more than 4 carry words and the FFT has a clm of 1 (which is 4 words).  If the
			// start_block in a section is not a multiple of num_postfft_blocks, then normval2
			// used in ynorm012_wpn will not properly correct the big/lit flags pointer.
			new_size = round_up_to_multiple_of (new_size, gwdata->num_postfft_blocks);

			// Peel off the ending blocks of the target section for this thread to start processing
			gwdata->pass1_carry_sections[i].start_block =
			gwdata->pass1_carry_sections[i].next_block = gwdata->pass1_carry_sections[target].last_block - new_size;
			gwdata->pass1_carry_sections[i].last_block = gwdata->pass1_carry_sections[target].last_block;
			gwdata->pass1_carry_sections[i].carry_out_section = gwdata->pass1_carry_sections[target].carry_out_section;
			gwdata->pass1_carry_sections[i].carry_in_blocks_finished = FALSE;	// Must be done AFTER changing start_block!

			// Shrink target section
			gwdata->pass1_carry_sections[target].last_block -= new_size;
			gwdata->pass1_carry_sections[target].carry_out_section = i;

			// Unlock target
			unlock_carry_section (gwdata, target);
			break;
		}
	}

	/* Set initial this_block, bump section's next block pointer, update section state.  Free lock. */
	asm_data->this_block = gwdata->pass1_carry_sections[i].next_block;
	gwdata->pass1_carry_sections[i].next_block += asm_data->cache_line_multiplier;
	gwdata->pass1_carry_sections[i].section_state = 1;

	/* Return the first block in this section */
	asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
	asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
	pass1_state1_norm_addrs (gwdata, asm_data);

	/* In SSE2 zero-padded FFT, set the upper half of the carries to zero instead */
	/* of XMM_BIGVAL.  This saves us a couple of clocks per FFT data element in xnorm_2d_zpad. */
	/* Ideally, we'd eliminate this code by fixing the add/sub and carry propagation code to also */
	/* expect zero in the upper half of each carry cache line (like we did in the AVX code). */
	if (gwdata->ZERO_PADDED_FFT && ! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX))) {
		int	i, carry_table_size;
		double	*table;
		carry_table_size = (gwdata->PASS1_SIZE) << 1;
		table = (double *) asm_data->carries;
		for (i = 0; i < carry_table_size; i += 8, table += 8) {
			table[4] = 0.0;
			table[5] = 0.0;
			table[6] = 0.0;
			table[7] = 0.0;
		}
	}

	/* Return success */
	return (TRUE);
}

/* Assign a thread's next block in pass 1 state 1. */

void pass1_state1_assign_next_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	/* Get thread number / section number */
	int i = asm_data->thread_num;

	/* Return the next block in this section (or first block in next section for carry propagation and possible postfft processing) */
	asm_data->next_block = gwdata->pass1_carry_sections[i].next_block;
	if (asm_data->next_block == gwdata->pass1_carry_sections[i].last_block && gwdata->pass1_carry_sections[i].section_state == 3)
		asm_data->next_block = asm_data->this_block;
	else if (asm_data->next_block == gwdata->num_pass1_blocks)
		asm_data->next_block = 0;

	/* Init prefetching for the next block */
	asm_data->data_prefetch = pass1_data_addr (gwdata, asm_data, asm_data->next_block);
	asm_data->premult_prefetch = pass1_premult_addr (gwdata, asm_data->next_block);
}

/* Assign a thread's first pass 2 block.  These are assigned in sequential order. */

static __inline void pass2_assign_first_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	asm_data->this_block = (int) atomic_fetch_incr (gwdata->next_block);
	asm_data->data_addr = pass2_data_addr (gwdata, asm_data, asm_data->this_block);
	asm_data->premult_addr = pass2_premult_addr (gwdata, asm_data->this_block);
}

/* Assign next available block in pass 2.  These are assigned in sequential order. */

static __inline void pass2_assign_next_block (
	gwhandle *gwdata,
	struct gwasm_data *asm_data)
{
	int next_block = (int) atomic_fetch_incr (gwdata->next_block);
	if (next_block < (int) gwdata->num_pass2_blocks) {
		asm_data->next_block = next_block;
		/* Init prefetching for the next block */
		asm_data->data_prefetch = pass2_data_addr (gwdata, asm_data, asm_data->next_block);
		asm_data->premult_prefetch = pass2_premult_addr (gwdata, asm_data->next_block);
	} else {
		asm_data->next_block = asm_data->this_block;
		asm_data->data_prefetch = asm_data->data_addr;
		asm_data->premult_prefetch = asm_data->premult_addr;
	}
}

/* Signal auxiliary threads that there is work to do */

void signal_auxiliary_threads (
	gwhandle *gwdata)
{
	// The parent, if any, manages threads
	gwhandle *parent_gwdata = (gwdata->parent_gwdata != NULL) ? gwdata->parent_gwdata : gwdata;
	parent_gwdata->active_child_gwdata = gwdata;			// Set pointer to the active child
	parent_gwdata->all_work_assigned = FALSE;			// When this is set and num_active_helpers reaches 0, it is safe to signal main thread
	gwevent_signal (&parent_gwdata->work_to_do);			// Start all helper threads
	if (parent_gwdata->use_spin_wait >= 2) atomic_set (parent_gwdata->alt_work_to_do, 1);
}

/* Wait for auxiliary threads to complete */

void wait_on_auxiliary_threads (
	gwhandle *gwdata)
{
	// The parent, if any, manages threads
	if (gwdata->parent_gwdata != NULL) gwdata = gwdata->parent_gwdata;
	// Set no more work to do state
	gwdata->all_work_assigned = TRUE;
	gwevent_reset (&gwdata->work_to_do);
	if (gwdata->use_spin_wait >= 2) atomic_set (gwdata->alt_work_to_do, 0);
	// Wait for helpers to finish work in progress
	if (gwdata->use_spin_wait) atomic_spinwait (gwdata->num_active_helpers, 0);
	else while (atomic_get (gwdata->num_active_helpers)) {
		gwevent_reset (&gwdata->all_helpers_done);
		if (atomic_get (gwdata->num_active_helpers)) gwevent_wait (&gwdata->all_helpers_done, 0);
	}
}

/* Routine for auxiliary threads */

struct thread_data {
	gwhandle *gwdata;
	int	thread_num;
};

void auxiliary_thread (void *arg)
{
	gwhandle *gwdata;
	int	thread_num;

/* Get information out of the passed in structure */

	struct thread_data *info = (struct thread_data *) arg;
	gwdata = info->gwdata;
	thread_num = info->thread_num;
	free (arg);

/* Call optional user provided callback routine so that the caller can set the thread's priority and affinity */

	if (gwdata->thread_callback != NULL)
		(*gwdata->thread_callback) (thread_num, 0, gwdata->thread_callback_data);

/* Loop waiting for work to do.  The main thread signals work_to_do event whenever there is work for the auxiliary thread(s) to do. */

	for ( ; ; ) {

	    // Wait on the work-to-do event for more work (or termination).  There are two different ways to wait for work.
	    if (gwdata->use_spin_wait > thread_num) atomic_spinwait (gwdata->alt_work_to_do, 1);
	    else gwevent_wait (&gwdata->work_to_do, 0);

/* If threads are to exit, break out of this work loop */

	    if (gwdata->helpers_must_exit) break;

/* IMPORTANT:  Helpers are considered done as soon as all_work_assigned is set and num_active_helpers reaches zero.  When this happens the main thread is */
/* allowed to resume.  However, there may be some helpers that have not yet been given a time slice after the work_to_do event was signalled!  We  must */
/* catch these workers right here.  If they were to get past this section, they could read some inconsistent state that the main thread has changed in */
/* preparation for the next batch of work for the helper threads. */

	    if (gwdata->all_work_assigned) continue;
	    atomic_incr (gwdata->num_active_helpers);
	    if (!gwdata->all_work_assigned) {			// all_work_assigned set should be rare
		struct gwasm_data *asm_data, *main_thread_asm_data;

/* Switch to the active child gwdata */

		gwdata = gwdata->active_child_gwdata;
		main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;
		asm_data = (struct gwasm_data *) ((char *) gwdata->thread_allocs[thread_num-1] + NEW_STACK_SIZE);

/* Each thread needs its own copy of the asm_data.  Each thread needs its own stack too.  There are a few more items that must be initialized for each thread. */

		if (!asm_data->aux_initialized) {
			memcpy (asm_data, main_thread_asm_data, sizeof (struct gwasm_data));

/* Set the thread number so that the assembly code can differentiate between the main thread and an auxiliary thread. */

			asm_data->thread_num = thread_num;

/* Each auxiliary thread needs its own pass 1 scratch area.  It was allocated right after the asm_data. */

			asm_data->scratch_area = align_ptr ((char *) asm_data + sizeof (struct gwasm_data), 128);

/* Init each auxiliary thread's carries area.  It was allocated right after the scratch area. */

			asm_data->carries = align_ptr ((char *) asm_data->scratch_area + gwdata->SCRATCH_SIZE, 128);
			init_asm_data_carries (gwdata, asm_data);

/* Auxiliary asm_data initialized */

			asm_data->aux_initialized = TRUE;
		}
		    
/* If we are to do add/sub/addsub/smallmul work, copy a little bit of state and go do the work */

		if (gwdata->pass1_state == PASS1_STATE_MULTITHREAD_OP) {
			asm_data->DBLARG = main_thread_asm_data->DBLARG;
			do_multithread_op_work (gwdata, asm_data);
			goto aux_out_of_work;
		}

/* Copy the main thread's asm_data's DESTARG for proper next_block address calculations.  We'll copy more asm_data later. */

		asm_data->DESTARG = main_thread_asm_data->DESTARG;

/* Get an available block for this thread to process (store it in the this_block field). */
/* NOTE: There is no guarantee that there is an available block to process. */

		if (gwdata->pass1_state == 0) {
			pass1_state0_assign_first_block (gwdata, asm_data);
			if (asm_data->this_block >= gwdata->num_pass1_blocks) goto aux_out_of_work;
			pass1_state0_assign_next_block (gwdata, asm_data);
		} else if (gwdata->pass1_state == 1) {
			if (! pass1_state1_assign_first_block (gwdata, asm_data)) goto aux_out_of_work;
			pass1_state1_assign_next_block (gwdata, asm_data);
		} else {
			pass2_assign_first_block (gwdata, asm_data);
			if (asm_data->this_block >= gwdata->num_pass2_blocks) goto aux_out_of_work;
			pass2_assign_next_block (gwdata, asm_data);
		}

/* Copy some data from the main thread's asm_data to this thread.  We only need to copy data that can change for each multiply call. */

		asm_data->DIST_TO_FFTSRCARG = main_thread_asm_data->DIST_TO_FFTSRCARG;
		asm_data->DIST_TO_MULSRCARG = main_thread_asm_data->DIST_TO_MULSRCARG;
		asm_data->SRC2ARG = main_thread_asm_data->SRC2ARG;
		asm_data->SRC3ARG = main_thread_asm_data->SRC3ARG;
		asm_data->DEST2ARG = main_thread_asm_data->DEST2ARG;
		asm_data->ffttype = main_thread_asm_data->ffttype;
		asm_data->mul4_opcode = main_thread_asm_data->mul4_opcode;
		asm_data->thread_work_routine = main_thread_asm_data->thread_work_routine;
		if (gwdata->pass1_state == 1) {
			asm_data->NORMRTN = main_thread_asm_data->NORMRTN;
			asm_data->zero_fft = main_thread_asm_data->zero_fft;
			asm_data->const_fft = main_thread_asm_data->const_fft;
			asm_data->add_sub_smallmul_op = main_thread_asm_data->add_sub_smallmul_op;
			asm_data->ADDIN_ROW = main_thread_asm_data->ADDIN_ROW;
			asm_data->ADDIN_OFFSET = main_thread_asm_data->ADDIN_OFFSET;
			asm_data->ADDIN_VALUE = main_thread_asm_data->ADDIN_VALUE;
			asm_data->POSTADDIN_VALUE = main_thread_asm_data->POSTADDIN_VALUE;
			if (gwdata->cpu_flags & CPU_AVX512F) {
				asm_data->u.zmm.ZMM_MULCONST = main_thread_asm_data->u.zmm.ZMM_MULCONST;
				asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE = main_thread_asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE;
				asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_SMALL_BASE = main_thread_asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_SMALL_BASE;
				asm_data->u.zmm.ZMM_K_TIMES_MULCONST_LO = main_thread_asm_data->u.zmm.ZMM_K_TIMES_MULCONST_LO;
				asm_data->u.zmm.ZMM_MINUS_C_TIMES_MULCONST = main_thread_asm_data->u.zmm.ZMM_MINUS_C_TIMES_MULCONST;
			} else if (gwdata->cpu_flags & CPU_AVX) {
				memcpy (asm_data->u.ymm.YMM_MULCONST, main_thread_asm_data->u.ymm.YMM_MULCONST, 4 * sizeof (double));
				memcpy (asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI, main_thread_asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI, 4 * sizeof (double));
				memcpy (asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO, main_thread_asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO, 4 * sizeof (double));
				memcpy (asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST, main_thread_asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST, 4 * sizeof (double));
			} else if (gwdata->cpu_flags & CPU_SSE2) {
				memcpy (asm_data->u.xmm.XMM_MULCONST, main_thread_asm_data->u.xmm.XMM_MULCONST, 2 * sizeof (double));
				memcpy (asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI, main_thread_asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI, 2 * sizeof (double));
				memcpy (asm_data->u.xmm.XMM_K_TIMES_MULCONST_LO, main_thread_asm_data->u.xmm.XMM_K_TIMES_MULCONST_LO, 2 * sizeof (double));
				memcpy (asm_data->u.xmm.XMM_MINUS_C_TIMES_MULCONST, main_thread_asm_data->u.xmm.XMM_MINUS_C_TIMES_MULCONST, 2 * sizeof (double));
			}
			// Copy maxerr and sumout
			if (gwdata->cpu_flags & CPU_AVX512F) {
				asm_data->MAXERR = main_thread_asm_data->MAXERR;
			} else if (gwdata->cpu_flags & CPU_AVX) {
				memcpy (asm_data->u.ymm.YMM_MAXERR, &main_thread_asm_data->u.ymm.YMM_MAXERR, 4 * sizeof (double));
			} else {
				asm_data->u.xmm.XMM_SUMOUT[0] = 0.0;
				asm_data->u.xmm.XMM_SUMOUT[1] = 0.0;
				memcpy (asm_data->u.xmm.XMM_MAXERR, &main_thread_asm_data->u.xmm.XMM_MAXERR, 2 * sizeof (double));
			}
		}

/* Now call the assembly code to do some work! */

		if (gwdata->pass1_state < PASS1_STATE_PASS2)
			pass1_aux_entry_point (asm_data);
		else
			pass2_aux_entry_point (asm_data);

/* The auxiliary thread has run out of work.  Decrement the count of number of active auxiliary threads. */
/* Signal all threads done when last auxiliary thread is done. */

aux_out_of_work:
		// Switch back to the parent gwdata
		if (gwdata->parent_gwdata != NULL) gwdata = gwdata->parent_gwdata;

		// No more work to assign to helper threads
		gwdata->all_work_assigned = TRUE;		// Set flag so any helper threads that have not yet started don't try to do any work
		gwevent_reset (&gwdata->work_to_do);		// Reset event saying there is work for helper threads to do
		if (gwdata->use_spin_wait >= 2) atomic_set (gwdata->alt_work_to_do, 0);
	    }

	    // When num active helpers reaches zero, main thread can wake up (either by waiting on all_helpers_done or via a spin wait)
	    // WARNING: It is possible for a straggler thread from the previous work_to_do event to lose its time slice just before the all_helpers_done signal call.
	    // This can cause spurious all_helpers_done signals for the current work_to_do event.  The main thread must be very careful resuming from the
	    // all_helpers_done wait by checking that num_active_helpers is in fact zero.
	    if (atomic_decr_fetch (gwdata->num_active_helpers) == 0) gwevent_signal (&gwdata->all_helpers_done);
	}

/* Call optional user provided callback routine so that the caller can do any necessary cleanup. */

	if (gwdata->thread_callback != NULL)
		(*gwdata->thread_callback) (thread_num, 1, gwdata->thread_callback_data);
}


/* This routine is called by the main thread assembly code to fire up all the auxiliary worker threads in pass 1. */

void pass1_wake_up_threads (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;

/* Init pass1_state (kludge: passed in next_block parameter) */
/* State is either 0 (forward FFT only) or 1 (inverse FFT and */
/* optional next forward FFT) */

	gwdata->pass1_state = asm_data->next_block;

/* Initialization for the forward FFT case */

	if (gwdata->pass1_state == 0) {

/* Prior to doing a forward zero-padded FFT copy 7 FFT elements */

		if (gwdata->ZERO_PADDED_FFT) xcopy_7_words (asm_data);

/* Set up the this_block and next_block values for the main thread */

		atomic_set (gwdata->next_block, 0);
		pass1_state0_assign_first_block (gwdata, asm_data);
		pass1_state0_assign_next_block (gwdata, asm_data);
	}

/* Initialization for the inverse FFT (and optional next forward FFT) case */

	if (gwdata->pass1_state == 1) {

/* For the two destination case, switch from writing the FFT data to writing the multiplication data. */

		asm_data->DESTARG = (char *) asm_data->DESTARG + (intptr_t) asm_data->DEST2ARG;

/* Initialize values that the normalization / carry propagation code uses. */

		if (gwdata->cpu_flags & CPU_AVX512F) {
		}
		else if (gwdata->cpu_flags & CPU_AVX) {
			asm_data->u.ymm.YMM_MAXERR[0] =
			asm_data->u.ymm.YMM_MAXERR[1] =
			asm_data->u.ymm.YMM_MAXERR[2] =
			asm_data->u.ymm.YMM_MAXERR[3] = asm_data->MAXERR;
		} else {
			((double *) asm_data->DESTARG)[-3] = 0.0; // For accumulating SUMOUT
			asm_data->u.xmm.XMM_SUMOUT[0] =
			asm_data->u.xmm.XMM_SUMOUT[1] = 0.0;
			asm_data->u.xmm.XMM_MAXERR[0] =
			asm_data->u.xmm.XMM_MAXERR[1] = asm_data->MAXERR;
		}

/* Create one big section containing all the pass 1 blocks.  Auxiliary threads will split this big section as each thread activates. */

		memset (gwdata->pass1_carry_sections, 0, gwdata->num_threads * sizeof (struct pass1_carry_sections));
		gwdata->pass1_carry_sections[0].last_block = gwdata->num_pass1_blocks;
		gwdata->pass1_carry_sections_unallocated = gwdata->num_threads - 1;

/* Set up the this_block and next_block values for the main thread */

		pass1_state1_assign_first_block (gwdata, asm_data);
		pass1_state1_assign_next_block (gwdata, asm_data);
	}

/* Signal the auxiliary threads to resume working */

	if (gwdata->num_threads > 1) signal_auxiliary_threads (gwdata);
}

/* This routine is called before normalizing a block of data. */
/* This gives us an opportunity to make some adjustments for addin values and zero-padded FFTs. */

void pass1_pre_carries (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;

/* For non-zero-padded FFTs, handle the pre-mul-by-const ADDIN_VALUE here */

	if (asm_data->ADDIN_VALUE != 0.0 && !gwdata->ZERO_PADDED_FFT && asm_data->this_block == asm_data->ADDIN_ROW) {
		double *fft_data = gwdata->SCRATCH_SIZE ? (double *) asm_data->scratch_area : (double *) asm_data->data_addr;
		fft_data = (double *) ((char *) fft_data + asm_data->ADDIN_OFFSET);
		*fft_data += asm_data->ADDIN_VALUE;
	}

/* In zero-padded FFTs, we must subtract ZPAD0-6 from the first seven result words. */
/* In the radix-4 delay with partial normalization also apply the partial normalization multipliers to ZPAD0-6. */

	if (gwdata->ZERO_PADDED_FFT && asm_data->this_block <= 6) {
		struct gwasm_data *main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;

		double *fft_data = gwdata->SCRATCH_SIZE ? (double *) asm_data->scratch_area : (double *) asm_data->DESTARG;
		for (unsigned int i = asm_data->this_block; i <= 6 && i < asm_data->this_block + asm_data->cache_line_multiplier; i++) {
			if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
				main_thread_asm_data->ZPAD0_6[i] *= gwdata->ZPAD_0_6_ADJUST[i];
				*fft_data -= main_thread_asm_data->ZPAD0_6[i];
			} else {
				*fft_data -= main_thread_asm_data->ZPAD0_6[i] * asm_data->u.xmm.XMM_NORM012_FF[0];
				asm_data->u.xmm.XMM_SUMOUT[0] += main_thread_asm_data->ZPAD0_6[i] * asm_data->u.xmm.XMM_NORM012_FF[0];
			}
			if (gwdata->cpu_flags & CPU_AVX512F)
				fft_data += 16;		// Next low-word/high-word pair (two cache lines)
			else
				fft_data += 8;		// Next cache line
		}
	}
}

/* This routine is called by the pass 1 threads when they've completed normalization */

#define PASS1_CARRIES_NO_FORWARD_FFT	0
#define PASS1_CARRIES_FORWARD_FFT	1

int pass1_post_carries (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;
	int	i;

/* For non-zero-padded FFTs, handle the post-mul-by-const ADDIN_VALUE here */

	if (asm_data->POSTADDIN_VALUE != 0.0 && !gwdata->ZERO_PADDED_FFT && asm_data->this_block == asm_data->ADDIN_ROW) {
		double *fft_data = gwdata->SCRATCH_SIZE ? (double *) asm_data->scratch_area : (double *) asm_data->data_addr;
		fft_data = (double *) ((char *) fft_data + asm_data->ADDIN_OFFSET);
		*fft_data += asm_data->POSTADDIN_VALUE;
	}

/* If postfft is not set, then do not perform forward FFT on the result */

	if (!gwdata->POSTFFT) return (PASS1_CARRIES_NO_FORWARD_FFT);

/* We must delay the forward FFT for the first few data blocks in each */
/* section (until the carries are added back in). */

	i = asm_data->thread_num;
	if (asm_data->this_block < gwdata->pass1_carry_sections[i].start_block + gwdata->num_postfft_blocks)
		return (PASS1_CARRIES_NO_FORWARD_FFT);

/* If this is a zero padded FFT and we are doing the last 3 blocks, then */
/* we need to copy a few of the data values before they are FFTed. */

	if (gwdata->ZERO_PADDED_FFT && asm_data->this_block + asm_data->cache_line_multiplier >= gwdata->num_pass1_blocks - 3)
		xcopy_3_words (asm_data);

/* Perform the forward FFT on these data blocks while they are still in the cache */

	return (PASS1_CARRIES_FORWARD_FFT);
}

/* This routine is called by assembly code threads to get the next */
/* pass 1 block to process.  It returns a code indicating what part */
/* of the pass 1 to do next. */

/* Return codes: */

#define PASS1_DO_MORE_INVERSE_FFT	0
#define PASS1_DO_MORE_FORWARD_FFT	1
#define PASS1_COMPLETE			2
#define PASS1_EXIT_THREAD		3
#define PASS1_START_PASS2		4
#define PASS1_DO_GWCARRIES		5

/* Pass 1 states: */
/* 0 = forward fft */
/* 1 = inverse fft */
/* 999 = not in pass 1, we're doing pass 2 */

int pass1_get_next_block (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;

/* If state is zero, then we are doing the forward FFT.  In this case we */
/* can process the blocks in any order (much like we do in pass 2). */

	if (gwdata->pass1_state == 0) {

/* There are no more blocks to process when the next block is the same */
/* as the block just processed. */

		if (asm_data->this_block == asm_data->next_block) {
			asm_data->DIST_TO_FFTSRCARG = 0;
			return (PASS1_START_PASS2);
		}

/* There is more pass 1 work to do.  Get next available block (if any) for prefetching. */

		asm_data->this_block = asm_data->next_block;
		asm_data->data_addr = asm_data->data_prefetch;
		asm_data->premult_addr = asm_data->premult_prefetch;
		pass1_state0_assign_next_block (gwdata, asm_data);
		return (PASS1_DO_MORE_FORWARD_FFT);
	}

/* Otherwise, pass1_state is one and we are doing the inverse FFT (and */
/* if POSTFFT is set pass 1 of the forward FFT on the result). */

/* Pass 1 state 1 section states: */
/* 0 = section not yet started */
/* 1 = section being processed */
/* 2 = section is adding carries into the next section */
/* 3 = section is doing postfft processing of next section */
/* 4 = section is complete */

/* Handle the common case, we're in the middle of processing a section */

	ASSERTG (gwdata->pass1_carry_sections[0].section_state >= 1 && gwdata->pass1_carry_sections[0].section_state <= 3);
	if (gwdata->pass1_carry_sections[0].section_state == 1) {

/* If there is another block to process in the section, let's process it */

		if (gwdata->pass1_carry_sections[0].next_block != gwdata->pass1_carry_sections[0].last_block) {
			asm_data->this_block = gwdata->pass1_carry_sections[0].next_block;
			gwdata->pass1_carry_sections[0].next_block += asm_data->cache_line_multiplier;
			asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
			asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
			pass1_state1_norm_addrs (gwdata, asm_data);
			pass1_state1_assign_next_block (gwdata, asm_data);
			return (PASS1_DO_MORE_INVERSE_FFT);
		}

/* Handle the case where we just did the last block.  We now need to apply the carries back to the first blocks. */

		gwdata->pass1_carry_sections[0].section_state = 2;
		asm_data->this_block = 0;
		asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
		pass1_state1_carry_addrs (gwdata, asm_data);
		return (PASS1_DO_GWCARRIES);
	}

/* Handle case where we just propagated carries into the next block. */

	if (gwdata->pass1_carry_sections[0].section_state == 2 && gwdata->POSTFFT) {
		gwdata->pass1_carry_sections[0].section_state = 3;

/* After the carries are added back in to the first block, save the lowest four words of a */
/* zero padded FFTs (before POSTFFT processing destroys the data). */

		if (gwdata->ZERO_PADDED_FFT) xcopy_4_words (asm_data);

/* Set blocks needing postfft processing */

		gwdata->pass1_carry_sections[0].start_block = 0;
		gwdata->pass1_carry_sections[0].last_block = gwdata->num_postfft_blocks;
		gwdata->pass1_carry_sections[0].next_block = 0;
	}

/* Do the forward FFT on the result blocks affected by our carries */

	if (gwdata->pass1_carry_sections[0].section_state == 3 &&
	    gwdata->pass1_carry_sections[0].next_block != gwdata->pass1_carry_sections[0].last_block) {
		asm_data->this_block = gwdata->pass1_carry_sections[0].next_block;
		gwdata->pass1_carry_sections[0].next_block += asm_data->cache_line_multiplier;
		asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
		asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
		pass1_state1_assign_next_block (gwdata, asm_data);
		return (PASS1_DO_MORE_FORWARD_FFT);
	}

/* We're finished, do final cleanup */

	if (gwdata->cpu_flags & CPU_AVX512F) {
	} else if (gwdata->cpu_flags & CPU_AVX) {
		if (asm_data->u.ymm.YMM_MAXERR[0] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.ymm.YMM_MAXERR[0];
		if (asm_data->u.ymm.YMM_MAXERR[1] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.ymm.YMM_MAXERR[1];
		if (asm_data->u.ymm.YMM_MAXERR[2] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.ymm.YMM_MAXERR[2];
		if (asm_data->u.ymm.YMM_MAXERR[3] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.ymm.YMM_MAXERR[3];
	} else if (gwdata->cpu_flags & CPU_SSE2) {
		if (asm_data->u.xmm.XMM_MAXERR[0] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.xmm.XMM_MAXERR[0];
		if (asm_data->u.xmm.XMM_MAXERR[1] > asm_data->MAXERR) asm_data->MAXERR = asm_data->u.xmm.XMM_MAXERR[1];
		/* Accumulate SUMOUT */
		((double *) asm_data->DESTARG)[-3] = asm_data->u.xmm.XMM_SUMOUT[0] + asm_data->u.xmm.XMM_SUMOUT[1];
		/* Normalize SUMOUT value by multiplying by 1 / (fftlen/2). */
		((double *) asm_data->DESTARG)[-3] *= asm_data->ttmp_ff_inv;
	}

	return (PASS1_COMPLETE);
}

int pass1_get_next_block_mt (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;
	int	i;

/* If state is zero, then we are doing the forward FFT.  In this case we */
/* can process the blocks in any order (much like we do in pass 2). */

	if (gwdata->pass1_state == 0) {

/* There are no more blocks to process when the next block is the same as the block just processed.  In that case, if this is an auxiliary */
/* thread return code telling the assembly code to exit.  Otherwise this is the main thread, wait for auxiliary threads to complete. */

		if (asm_data->this_block == asm_data->next_block) {
			if (asm_data->thread_num) return (PASS1_EXIT_THREAD);
			wait_on_auxiliary_threads (gwdata);
			asm_data->DIST_TO_FFTSRCARG = 0;
			return (PASS1_START_PASS2);
		}

/* There is more pass 1 work to do.  Get next available block (if any) for prefetching. */

		asm_data->this_block = asm_data->next_block;
		asm_data->data_addr = asm_data->data_prefetch;
		asm_data->premult_addr = asm_data->premult_prefetch;
		pass1_state0_assign_next_block (gwdata, asm_data);
		return (PASS1_DO_MORE_FORWARD_FFT);
	}

/* Otherwise, pass1_state is one and we are doing the inverse FFT (and */
/* if POSTFFT is set pass 1 of the forward FFT on the result). */

/* Pass 1 state 1 section states: */
/* 0 = section not yet started */
/* 1 = section being processed */
/* 2 = section is adding carries into the next section */
/* 3 = section is doing postfft processing of next section */
/* 4 = section complete */

/* Handle the common case, we're in the middle of processing a section */

	i = asm_data->thread_num;
	ASSERTG (gwdata->pass1_carry_sections[i].section_state >= 1 && gwdata->pass1_carry_sections[i].section_state <= 3);
	if (gwdata->pass1_carry_sections[i].section_state == 1) {

/* If we just finished off the first blocks in the section, then set carry_in_blocks_finished. */
/* Signal an event in case the dependent section is waiting on us to finish processing our first blocks. */

		if (asm_data->this_block + asm_data->cache_line_multiplier == gwdata->pass1_carry_sections[i].start_block + gwdata->num_postfft_blocks) {
			gwdata->pass1_carry_sections[i].carry_in_blocks_finished = TRUE;
			gwevent_signal (&gwdata->can_carry_into);
		}

/* Temporarily prevent another section from splitting this section while we are examining and changing next block, etc.  Acquire a lock. */

		lock_carry_section (gwdata, i);

/* If there is another block to process in the section, let's process it */

		if (gwdata->pass1_carry_sections[i].next_block != gwdata->pass1_carry_sections[i].last_block) {
			// Set this_block to the block we've been prefetching.  Bump next block pointer.
			asm_data->this_block = gwdata->pass1_carry_sections[i].next_block;
			gwdata->pass1_carry_sections[i].next_block += asm_data->cache_line_multiplier;
			// We're done changing block numbers.  Unlock, set pointers and return to asm code.
			unlock_carry_section (gwdata, i);
			asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
			asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
			pass1_state1_norm_addrs (gwdata, asm_data);
			pass1_state1_assign_next_block (gwdata, asm_data);
			return (PASS1_DO_MORE_INVERSE_FFT);
		}

/* We just processed the last block in the section.  We now need to apply this section's carries to the next section.  Unfortunately, the next section */
/* may not have processed it's first blocks yet forcing us to wait.  Technically, each section should have its own event as there is no guarantee that */
/* the signal will wake up the thread waiting here.  However, waiting here should be rare as it only happens when one thread completes an entire section */
/* before the dependent thread has processed its first blocks. */
/* NOTE: The carry out section may have completed and is now processing a split-off section.  Thus, we must check both the carry_in_blocks_finished flag or */
/* mismatching last_block/first_block values.  ALSO, SECTION SPLITTING CODE MUST CHANGE THESE VALUES IN A PARTICULAR ORDER SUCH THAT THIS CHECK ALWAYS WORKS. */

// DANGER: This reset, recheck, and wait is fraught with danger!  The initial implementation hung in this scenario:
//	thread 1		thread 1a		thread 2		thread 2a
//	carry check fails
//	reset
//	carry check fails
//				sets carry finished
//				signals
//							carry check fails
//										sets carry finished
//										signals
//							reset
//							carry check succeeds
//	waits and hangs
// To work around this issue (no resets is equivalent to a spin loop, multiple resets leads to missed signals), I added code to atomicly increment a counter
// and do only one reset.

		gwdata->pass1_carry_sections[i].section_state = 2;
		unlock_carry_section (gwdata, i);

		int carry_out_section = gwdata->pass1_carry_sections[i].carry_out_section;
		int last_block = gwdata->pass1_carry_sections[i].last_block != gwdata->num_pass1_blocks ? gwdata->pass1_carry_sections[i].last_block : 0;
		while (last_block == gwdata->pass1_carry_sections[carry_out_section].start_block &&
		       !gwdata->pass1_carry_sections[carry_out_section].carry_in_blocks_finished) {
			gwmutex_lock (&gwdata->thread_lock);
			if (atomic_fetch_incr (gwdata->can_carry_into_counter) == 0) gwevent_reset (&gwdata->can_carry_into);
			gwmutex_unlock (&gwdata->thread_lock);
			if (last_block != gwdata->pass1_carry_sections[carry_out_section].start_block ||
			    gwdata->pass1_carry_sections[carry_out_section].carry_in_blocks_finished) { atomic_decr (gwdata->can_carry_into_counter); break; }
			gwevent_wait (&gwdata->can_carry_into, 0);
			atomic_decr (gwdata->can_carry_into_counter);
		}

/* Now apply this section's carries to the next section. */

		asm_data->this_block = gwdata->pass1_carry_sections[i].last_block;
		if (asm_data->this_block == gwdata->num_pass1_blocks) asm_data->this_block = 0;
		/* Copy the 7 ZPAD values that were computed in the main thread */
		if (gwdata->ZERO_PADDED_FFT && asm_data->this_block == 0) {
			struct gwasm_data *main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;
			memcpy (asm_data->ZPAD0_6, main_thread_asm_data->ZPAD0_6, 7 * sizeof (double));
		}
		asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
		pass1_state1_carry_addrs (gwdata, asm_data);
		return (PASS1_DO_GWCARRIES);
	}

/* Handle case where we just propagated carries into the next block and we need to start the */
/* forward FFT on the affected blocks. */

	if (gwdata->pass1_carry_sections[i].section_state == 2 && gwdata->POSTFFT) {
		gwdata->pass1_carry_sections[i].section_state = 3;

/* After the carries are added back in to the first blocks, save the lowest four words of a */
/* zero padded FFTs (before POSTFFT processing destroys the data). */

		if (gwdata->ZERO_PADDED_FFT) {
			if (asm_data->this_block == 0) xcopy_4_words (asm_data);

/* After the carries are added back in to the last blocks, save the three words below the halfway */
/* point in a zero padded FFTs (before POSTFFT processing destroys the data).  This can only happen */
/* due to section splitting or a huge number of threads. */

			if (asm_data->this_block + gwdata->num_postfft_blocks >= gwdata->num_pass1_blocks - 3)
				xcopy_3_words_after_gwcarries (asm_data);
		}

/* Figure out which blocks need postfft processing */

		gwdata->pass1_carry_sections[i].start_block = gwdata->pass1_carry_sections[i].next_block;
		if (gwdata->pass1_carry_sections[i].start_block == gwdata->num_pass1_blocks) gwdata->pass1_carry_sections[i].start_block = 0;
		gwdata->pass1_carry_sections[i].last_block = gwdata->pass1_carry_sections[i].start_block + gwdata->num_postfft_blocks;
		gwdata->pass1_carry_sections[i].next_block = gwdata->pass1_carry_sections[i].start_block;
	}

/* Do the forward FFT on the postfft blocks */

	if (gwdata->pass1_carry_sections[i].section_state == 3 &&
	    gwdata->pass1_carry_sections[i].next_block != gwdata->pass1_carry_sections[i].last_block) {
		asm_data->this_block = gwdata->pass1_carry_sections[i].next_block;
		gwdata->pass1_carry_sections[i].next_block += asm_data->cache_line_multiplier;
		asm_data->data_addr = pass1_data_addr (gwdata, asm_data, asm_data->this_block);
		asm_data->premult_addr = pass1_premult_addr (gwdata, asm_data->this_block);
		pass1_state1_assign_next_block (gwdata, asm_data);
		return (PASS1_DO_MORE_FORWARD_FFT);
	}

/* We've finished this section */

	gwdata->pass1_carry_sections[i].section_state = 4;

/* See if there is some other section we can help finish off which will set the section_state back to 1 */

	if (pass1_state1_assign_first_block (gwdata, asm_data)) {
		pass1_state1_assign_next_block (gwdata, asm_data);
		return (PASS1_DO_MORE_INVERSE_FFT);
	}

/* We've finished this thread, merge this thread's maxerr and sumout with the main thread */

#define replace_with_larger(a,b)	if ((a)<(b)) (a)=(b)
#define locked_replace_with_larger(a,b) if ((a)<(b)) { gwmutex_lock(&gwdata->thread_lock); replace_with_larger(a,b); gwmutex_unlock(&gwdata->thread_lock);}
	if (gwdata->cpu_flags & CPU_AVX512F) {
		struct gwasm_data *main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;
		locked_replace_with_larger (main_thread_asm_data->MAXERR, asm_data->MAXERR);
	} else if (gwdata->cpu_flags & CPU_AVX) {
		struct gwasm_data *main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;
		replace_with_larger (asm_data->u.ymm.YMM_MAXERR[0], asm_data->u.ymm.YMM_MAXERR[1]);
		replace_with_larger (asm_data->u.ymm.YMM_MAXERR[0], asm_data->u.ymm.YMM_MAXERR[2]);
		replace_with_larger (asm_data->u.ymm.YMM_MAXERR[0], asm_data->u.ymm.YMM_MAXERR[3]);
		locked_replace_with_larger (main_thread_asm_data->MAXERR, asm_data->u.ymm.YMM_MAXERR[0]);
	} else if (gwdata->cpu_flags & CPU_SSE2) {
		struct gwasm_data *main_thread_asm_data = (struct gwasm_data *) gwdata->asm_data;
		replace_with_larger (asm_data->u.xmm.XMM_MAXERR[0], asm_data->u.xmm.XMM_MAXERR[1]);
		locked_replace_with_larger (main_thread_asm_data->MAXERR, asm_data->u.xmm.XMM_MAXERR[0]);
		/* Accumulate SUMOUT */
		gwmutex_lock (&gwdata->thread_lock);
		((double *) asm_data->DESTARG)[-3] += asm_data->u.xmm.XMM_SUMOUT[0] + asm_data->u.xmm.XMM_SUMOUT[1];
		gwmutex_unlock (&gwdata->thread_lock);
	}

/* There are no more blocks to process.  If this is an auxiliary thread then return code telling the assembly code to exit. */

	if (asm_data->thread_num) return (PASS1_EXIT_THREAD);

/* There are no more blocks to process.  This is the main thread, wait for auxiliary threads to complete. */

	wait_on_auxiliary_threads (gwdata);

/* We're finished, do final cleanup */

	/* Normalize SUMOUT value by multiplying by 1 / (fftlen/2). */
	((double *) asm_data->DESTARG)[-3] *= asm_data->ttmp_ff_inv;

	return (PASS1_COMPLETE);
}

/* This callback routine is called by the main thread assembly code to fire up the auxiliary worker threads in pass 2. */

void pass2_wake_up_threads (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;

/* Call assign_block twice to set up the main thread's this_block and next_block values. */

	gwdata->pass1_state = PASS1_STATE_PASS2;
	atomic_set (gwdata->next_block, 0);
	pass2_assign_first_block (gwdata, asm_data);
	pass2_assign_next_block (gwdata, asm_data);

/* Signal the auxiliary threads to resume working */

	if (gwdata->num_threads > 1 && gwdata->PASS1_SIZE) signal_auxiliary_threads (gwdata);
}

/* This routine is called by assembly code threads to get the next pass 2 block to process. */

int pass2_get_next_block (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata;

	if (asm_data->this_block == asm_data->next_block) {
		asm_data->DIST_TO_FFTSRCARG = 0;
		return (TRUE);
	}

	gwdata = asm_data->gwdata;

	asm_data->this_block = asm_data->next_block;
	asm_data->data_addr = asm_data->data_prefetch;
	asm_data->premult_addr = asm_data->premult_prefetch;
	pass2_assign_next_block (gwdata, asm_data);

	return (FALSE);
}

/* This is the multi-thread version of the routine above */

int pass2_get_next_block_mt (
	struct gwasm_data *asm_data)
{
	gwhandle *gwdata = asm_data->gwdata;

/* There are no more blocks to process when the next block is the same */
/* as the block just processed.  In that case, if this is the main thread */
/* wait for auxiliary threads to complete.  If this is an auxiliary thread */
/* then return code telling the assembly code to exit. */

	if (asm_data->this_block == asm_data->next_block) {
		if (asm_data->thread_num) return (TRUE);
		wait_on_auxiliary_threads (gwdata);
		asm_data->DIST_TO_FFTSRCARG = 0;
		return (TRUE);
	}

/* Copy prefetched block and addresses to this block.  Get next available block to prefetch. */

	asm_data->this_block = asm_data->next_block;
	asm_data->data_addr = asm_data->data_prefetch;
	asm_data->premult_addr = asm_data->premult_prefetch;
	pass2_assign_next_block (gwdata, asm_data);

/* Return code indicating more work to do */

	return (FALSE);
}


/* Perform initializations required for multi-threaded operation */

int multithread_init (
	gwhandle *gwdata)
{
	struct gwasm_data *asm_data;
	unsigned int i;

/* Only two pass AVX-512/AVX/SSE2 FFTs support multi-threaded execution */

	if (gwdata->PASS2_SIZE == 0 || !(gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2))) {
		gwdata->num_threads = 1;
		return (0);
	}

/* GENERAL_MMGW_MOD does not have an asm_data structure to initialize */

	if (!gwdata->GENERAL_MMGW_MOD) {

/* Get pointer to assembly structure */

		asm_data = (struct gwasm_data *) gwdata->asm_data;

/* Save gwdata pointer in asm_data so that C callback routines can access gwdata.  Set flag indicating this is the main thread. */

		asm_data->gwdata = gwdata;
		asm_data->thread_num = 0;

/* Init other variables */

		if (gwdata->cpu_flags & CPU_AVX512F) {
			if (gwdata->PASS1_SIZE == 0) {
				gwdata->num_pass1_blocks = 1;
				gwdata->num_pass2_blocks = 1;
				asm_data->last_pass1_block = 0;
			} else {
				gwdata->num_pass1_blocks = gwdata->PASS2_SIZE;
				gwdata->num_pass2_blocks = gwdata->PASS1_SIZE >> 1;
				asm_data->last_pass1_block = gwdata->num_pass1_blocks - asm_data->cache_line_multiplier;
			}
		}
		else if (gwdata->cpu_flags & CPU_AVX) {
			gwdata->num_pass1_blocks = gwdata->PASS2_SIZE;
			gwdata->num_pass2_blocks = gwdata->PASS1_SIZE >> 1;
			asm_data->last_pass1_block = gwdata->num_pass1_blocks - asm_data->cache_line_multiplier;
		} else {
			gwdata->num_pass1_blocks = gwdata->PASS2_SIZE >> 1;
			gwdata->num_pass2_blocks = gwdata->PASS1_SIZE >> 2;
			asm_data->last_pass1_block = gwdata->num_pass1_blocks - asm_data->cache_line_multiplier;
		}

/* Place a limit on the number of threads */

		if (gwdata->num_threads > gwdata->num_pass1_blocks / asm_data->cache_line_multiplier)
			gwdata->num_threads = gwdata->num_pass1_blocks / asm_data->cache_line_multiplier;
		if (gwdata->num_threads > gwdata->num_pass1_blocks / 8)
			gwdata->num_threads = gwdata->num_pass1_blocks / 8;
		if (gwdata->num_threads == 0) gwdata->num_threads = 1;

/* Determine how many data blocks are affected by carries out of pass 1 section.  Zero-padded FFTs require 8 words */
/* to propagate carries into.  For AVX-512 FFTs, znorm012_wpn can spread the carry over a maximum of 8 words. */
/* For AVX FFTs, ynorm012_wpn can spread the carry over a maximum of either 4 or 8 words. */
/* For SSE2 FFTs, xnorm012_2d and xnorm012_2d_wpn spreads carries over either 2 or 6 words. */

		if (gwdata->ZERO_PADDED_FFT)
			gwdata->num_postfft_blocks = 8;
		else if (gwdata->cpu_flags & CPU_AVX512F) {
			gwdata->num_postfft_blocks = (int) floor (53.0 / (gwdata->avg_num_b_per_word * log2 (gwdata->b)));
			ASSERTG (gwdata->PASS1_SIZE == 0 || gwdata->num_postfft_blocks <= 8);
		} else if (gwdata->cpu_flags & CPU_AVX) {
			gwdata->num_postfft_blocks = (int) floor (53.0 / (gwdata->avg_num_b_per_word * log2 (gwdata->b)));
			ASSERTG (gwdata->num_postfft_blocks <= 8);
			asm_data->SPREAD_CARRY_OVER_EXTRA_WORDS = (gwdata->num_postfft_blocks > 4);
		} else {
			if (asm_data->SPREAD_CARRY_OVER_EXTRA_WORDS) gwdata->num_postfft_blocks = 6;
			else gwdata->num_postfft_blocks = 2;
		}
		gwdata->num_postfft_blocks = round_up_to_multiple_of (gwdata->num_postfft_blocks, asm_data->cache_line_multiplier);

/* Calculate the values used to compute pass 2 premultier pointers.  This only happens for our home-grown FFTs.  Non-power-of-2 pass 2 sizes are not supported. */
/* We calculate an adjusted starting address of the premultiplier data so that both real and negacyclic FFTs can use the same formula to */
/* calculate the proper address given a block number. */

		if (gwdata->FFT_TYPE == FFT_TYPE_HOME_GROWN) {
			gwdata->pass2_premult_block_size =
				(gwdata->PASS2_SIZE == 256) ? 32 * 128 :
				(gwdata->PASS2_SIZE == 1024) ? 64 * 128 :
				(gwdata->PASS2_SIZE == 2048) ? 96 * 128 :
				(gwdata->PASS2_SIZE == 4096) ? 128 * 128 :
				(gwdata->PASS2_SIZE == 8192) ? 192 * 128 : 0;
			if (gwdata->NEGACYCLIC_FFT)
				gwdata->adjusted_pass2_premults = asm_data->u.xmm.pass2_premults;
			else
				gwdata->adjusted_pass2_premults = (char *) asm_data->u.xmm.pass2_premults - gwdata->pass2_premult_block_size;
		}

/* Allocate carry section array */

		gwdata->pass1_carry_sections = (struct pass1_carry_sections *) malloc (gwdata->num_threads * sizeof (struct pass1_carry_sections));
		if (gwdata->pass1_carry_sections == NULL) return (GWERROR_MALLOC);

/* If we aren't multithreading, use the simpler version of routines */

		if (gwdata->num_threads <= 1) {
			asm_data->pass1_wake_up_threads = pass1_wake_up_threads;
			asm_data->pass1_pre_carries = pass1_pre_carries;
			asm_data->pass1_post_carries = pass1_post_carries;
			asm_data->pass1_get_next_block = pass1_get_next_block;
			asm_data->pass2_wake_up_threads = pass2_wake_up_threads;
			asm_data->pass2_get_next_block = pass2_get_next_block;
			return (0);
		}

/* Init mutexes, events, and atomics used to sync auxiliary threads */

		gwmutex_init (&gwdata->thread_lock);
		gwevent_init (&gwdata->can_carry_into);
		atomic_set (gwdata->can_carry_into_counter, 0);		// No one waiting on a can_carry_into signal

/* Set ptrs to call back routines in structure used by assembly code */

		asm_data->pass1_wake_up_threads = pass1_wake_up_threads;
		asm_data->pass1_pre_carries = pass1_pre_carries;
		asm_data->pass1_post_carries = pass1_post_carries;
		asm_data->pass1_get_next_block = pass1_get_next_block_mt;
		asm_data->pass2_wake_up_threads = pass2_wake_up_threads;
		asm_data->pass2_get_next_block = pass2_get_next_block_mt;

/* Init thread arrays */

		gwdata->thread_allocs = (gwthread *) malloc ((gwdata->num_threads - 1) * sizeof (void *));
		if (gwdata->thread_allocs == NULL) return (GWERROR_MALLOC);
		memset (gwdata->thread_allocs, 0, (gwdata->num_threads - 1) * sizeof (void *));

/* Allocate memory for each helper thread */

		for (i = 0; i < gwdata->num_threads - 1; i++) {
			/* Allocate memory for thread's stack, asm_data, scratch area, and carries */
			gwdata->thread_allocs[i] = aligned_malloc (NEW_STACK_SIZE + sizeof (struct gwasm_data) + 128 + gwdata->SCRATCH_SIZE + 128 + asm_data_carries_size (gwdata) * sizeof (double), 4096);
			if (gwdata->thread_allocs[i] == NULL) return (GWERROR_MALLOC);
			/* Set flag for auxilary thread to initialize the asm_data */
			struct gwasm_data *thread_asm_data = (struct gwasm_data *) ((char *) gwdata->thread_allocs[i] + NEW_STACK_SIZE);
			thread_asm_data->aux_initialized = FALSE;
		}
	}

/* Pre-create each auxiliary thread used in multiplication code. */

	if (gwdata->num_threads > 1 && gwdata->parent_gwdata == NULL) {

/* Init mutexes, events, and atomics used to control auxiliary threads */

		gwevent_init (&gwdata->work_to_do);
		gwevent_init (&gwdata->all_helpers_done);
		atomic_set (gwdata->alt_work_to_do, 0);			// No work for helpers to do yet
		gwdata->helpers_must_exit = FALSE;

/* Init thread arrays */

		gwdata->thread_ids = (gwthread *) malloc ((gwdata->num_threads - 1) * sizeof (gwthread));
		if (gwdata->thread_ids == NULL) return (GWERROR_MALLOC);
		memset (gwdata->thread_ids, 0, (gwdata->num_threads - 1) * sizeof (gwthread));

/* Launch each helper thread */

		for (i = 0; i < gwdata->num_threads - 1; i++) {
			struct thread_data *info;

			info = (struct thread_data *) malloc (sizeof (struct thread_data));
			if (info == NULL) return (GWERROR_MALLOC);
			info->gwdata = gwdata;
			info->thread_num = i+1;

			/* Launch the auxiliary thread */
			gwthread_create_waitable (&gwdata->thread_ids[i], &auxiliary_thread, info);
		}
	}

/* Return success */

	return (0);
}

/* Perform cleanup required by multi-threaded operation */

void multithread_term (
	gwhandle *gwdata)
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned int i;

/* If we are multithreading AND multithreading was initialized properly, then wait for compute threads to exit before we free memory. */

	if (gwdata->num_threads > 1 && gwdata->parent_gwdata == NULL) {

/* Set variable that tells all auxiliary threads and hyperthreads to terminate.  Fire up auxiliary compute threads so they can exit. */

		if (gwdata-> thread_ids != NULL) {
			gwdata->helpers_must_exit = TRUE;
			gwevent_signal (&gwdata->work_to_do);
			atomic_set (gwdata->alt_work_to_do, 1);

/* Wait for all compute threads to exit.  We must do this so that this thread can safely delete the gwdata structure */

			for (i = 0; i < gwdata->num_threads - 1; i++)
				if (gwdata->thread_ids[i]) gwthread_wait_for_exit (&gwdata->thread_ids[i]);
			free (gwdata->thread_ids), gwdata->thread_ids = NULL;
		}

/* Free up the multithread resources */

		gwevent_destroy (&gwdata->work_to_do);
		gwevent_destroy (&gwdata->all_helpers_done);
	}

/* Free up more memory and locks */

	if (gwdata->num_threads > 1 && !gwdata->GENERAL_MMGW_MOD) {
		if (gwdata->thread_allocs != NULL) {
			for (i = 0; i < gwdata->num_threads - 1; i++) aligned_free (gwdata->thread_allocs[i]);
			free (gwdata->thread_allocs), gwdata->thread_allocs = NULL;
		}
		gwmutex_destroy (&gwdata->thread_lock);
		gwevent_destroy (&gwdata->can_carry_into);
	}

/* Free up memory allocated by multithread_init */

	free (gwdata->pass1_carry_sections);
	gwdata->pass1_carry_sections = NULL;
}

/* Cleanup any memory allocated for multi-precision math */

void gwdone (
	gwhandle *gwdata)	/* Handle returned by gwsetup */
{
	// Multithreading cleanup
	multithread_term (gwdata);

	// Free allocated gwnums
	if (gwdata->clone_of == NULL) {
		free (gwdata->gwnum_free); gwdata->gwnum_free = NULL;
		if (gwdata->gwnum_alloc != NULL) {
			for (unsigned int i = 0; i < gwdata->gwnum_alloc_count; i++) {
				char	*p;
				int32_t	freeable;
				p = (char *) gwdata->gwnum_alloc[i];
				freeable = * (int32_t *) (p - 32);
				if (freeable & GWFREEABLE) {
					if (freeable & GWFREE_LARGE_PAGES) aligned_large_pages_free ((char *) p - GW_HEADER_SIZE (gwdata));
					else aligned_free ((char *) p - GW_HEADER_SIZE (gwdata));
				}
			}
			free (gwdata->gwnum_alloc); gwdata->gwnum_alloc = NULL;
		}
		while (gwdata->array_list != NULL) gwfree_array (gwdata, (gwnum *) gwdata->array_list);
		if (gwdata->large_pages_ptr != NULL) large_pages_free (gwdata->large_pages_ptr), gwdata->large_pages_ptr = NULL;
		else aligned_free (gwdata->gwnum_memory), gwdata->gwnum_memory = NULL;
	}

	// Free structures that are allocated for both original and cloned gwdatas
	term_ghandle (&gwdata->gdata);

	// Free items that are shared with cloned gwdatas (only free when parent is freed)
	if (gwdata->clone_of == NULL) {
		free (gwdata->GW_MODULUS); gwdata->GW_MODULUS = NULL;
		free (gwdata->dd_data); gwdata->dd_data = NULL;
		gwmutex_destroy (&gwdata->alloc_lock);
	}

	// Free items that are handled differently for original and cloned gwdatas
	if (gwdata->clone_of == NULL) {
		if (gwdata->asm_data != NULL) {
			struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
			unshare_sincos_data (asm_data->sincos1);			// SSE2
			unshare_sincos_data (asm_data->sincos2);			// SSE2 & AVX & AVX512
			unshare_sincos_data (asm_data->xsincos_complex);		// SSE2 & AVX & AVX512
			unshare_sincos_data (asm_data->sincos3);			// SSE2 & AVX & AVX512
			aligned_free ((char *) gwdata->asm_data - NEW_STACK_SIZE), gwdata->asm_data = NULL;
		}
		// Check that all clones have been terminated
		ASSERTG (gwdata->clone_count == 0);
	}
	else {
		if (gwdata->asm_data != NULL) {
			struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
			// One-pass AVX-512 FFTs set the scratch area pointer without allocating memory.
			if (gwdata->SCRATCH_SIZE) aligned_free (asm_data->scratch_area), asm_data->scratch_area = NULL;
			aligned_free (asm_data->carries), asm_data->carries = NULL;
			aligned_free ((char *) gwdata->asm_data - NEW_STACK_SIZE), gwdata->asm_data = NULL;
		}
		// Decrement the clone_count
		atomic_decr (gwdata->clone_of->clone_count);
	}

	// Free cached radix conversion data
	if (gwdata->to_radix_gwdata != NULL) {
		gwdone (gwdata->to_radix_gwdata);
		free (gwdata->to_radix_gwdata), gwdata->to_radix_gwdata = NULL;
	}
	if (gwdata->from_radix_gwdata != NULL) {
		gwdone (gwdata->from_radix_gwdata);
		free (gwdata->from_radix_gwdata), gwdata->from_radix_gwdata = NULL;
	}

	// Free general MMGW mod gwdatas
	if (gwdata->cyclic_gwdata != NULL) {
		gwdone (gwdata->cyclic_gwdata);
		free (gwdata->cyclic_gwdata), gwdata->cyclic_gwdata = NULL;
	}
	if (gwdata->negacyclic_gwdata != NULL) {
		gwdone (gwdata->negacyclic_gwdata);
		free (gwdata->negacyclic_gwdata), gwdata->negacyclic_gwdata = NULL;
	}
}

/* Routine to allocate aligned memory for our big numbers.  Memory is allocated on 128-byte boundaries, */
/* with an additional 32 bytes prior to the data for storing useful stuff. */

gwnum real_gwalloc (
	gwhandle *gwdata,
	int32_t	additional_freeable_flags)
{
	unsigned long header_size, size, aligned_size;
	char	*p, *q;
	int32_t	freeable;

/* Feed all allocations through the parent gwdata */

	if (gwdata->clone_of) gwdata = gwdata->clone_of;

/* Return cached gwnum if possible */

	if (gwdata->gwnum_free_count) {
		gwmutex_lock (&gwdata->alloc_lock);			// Obtain lock necessary for thread-safe operation
		if (gwdata->gwnum_free_count) {
			gwnum r = gwdata->gwnum_free[--gwdata->gwnum_free_count];
			gwmutex_unlock (&gwdata->alloc_lock);
			// For consistency with freshly allocated gwnums, clear header words
			* (uint32_t *) ((char *) r - 28) = 0;		/* Has-been-pre-ffted flag */
			* (double *) ((char *) r - 16) = 0.0;		/* SUM(INPUTS) */
			* (double *) ((char *) r - 24) = 0.0;		/* SUM(OUTPUTS) */
			* (int32_t *) ((char *) r - 32) |= additional_freeable_flags;
			unnorms (r) = 0.0f;				/* Unnormalized adds count */
			return (r);
		}
		gwmutex_unlock (&gwdata->alloc_lock);
	}

/* Compute the amount of memory to allocate.  Allocate 96 extra bytes for header information and align the data appropriately. */
/* When allocating memory out of the big buffer for a torture test, allocate on 128 byte boundaries to maximize the number of gwnums allocated. */

	header_size = GW_HEADER_SIZE (gwdata);
	size = gwnum_datasize (gwdata);
	aligned_size = round_up_to_multiple_of (header_size, 128) + round_up_to_multiple_of (size, 128);
	p = NULL;

/* First option is the one gwnum that was allocated along with the sin/cos tables.  This special gwnum is allocated before any */
/* cloning can happen, thus the multithreading lock is not necessary. */

	if (gwdata->large_pages_gwnum != NULL) {
		p = (char *) gwdata->large_pages_gwnum;
		gwdata->large_pages_gwnum = NULL;
		p += round_up_to_multiple_of (header_size, 128) - header_size;
		freeable = 0;
	}

/* Second option is allocating from the kludgy big buffer (torture testing).  This feature is used by prime95 torture testing (and should be */
/* replaced by gwalloc_array).  Torture testing does not use gwclone, thus the multithreading lock is not necessary. */

	else if (gwdata->GW_BIGBUF_SIZE >= aligned_size) {
		p = gwdata->GW_BIGBUF;
		gwdata->GW_BIGBUF += aligned_size;
		gwdata->GW_BIGBUF_SIZE -= aligned_size;
		p += round_up_to_multiple_of (header_size, 128) - header_size;
		freeable = 0;
	}

/* Third option is to allocate using large pages */

	else if (gwdata->use_large_pages) {
		// Randomize the starting cache line in 64KB blocks.  Perhaps that will help with potential 64KB cache collisions.
		int	alignment = gwdata->GW_ALIGNMENT;
		int	alignment_mod = (header_size - gwdata->GW_ALIGNMENT_MOD) & (gwdata->GW_ALIGNMENT - 1);
		if (alignment < 65536 && 65536 % alignment == 0) {
			alignment_mod += (rand () % (65536 / alignment)) * alignment;
			alignment = 65536;
		}
		p = (char *) aligned_offset_large_pages_malloc (size + header_size, alignment, alignment_mod);
		freeable = GWFREEABLE + GWFREE_LARGE_PAGES;
	}

/* Fourth option is to use plain old malloc with alignment (used as a fallback to failed large pages alloc) */
/* FreeBSD supports supports large pages (superpages) automatically.  Allocate the first gwnum with a 4MB alignment to minimize */
/* fragmentation across superpage boundaries.  The theory is this will maximize the number of superpage promotions. */
#ifdef __FreeBSD__
	if (p == NULL && size >= 0x400000 && gwdata->gwnum_alloc_count == 0) {
		p = (char *) aligned_offset_malloc (size + header_size, 0x400000, (header_size - gwdata->GW_ALIGNMENT_MOD) & 0x3FFFFF);
		freeable = GWFREEABLE;
	}
#endif
	if (p == NULL) {
		p = (char *) aligned_offset_malloc (size + header_size, gwdata->GW_ALIGNMENT, (header_size - gwdata->GW_ALIGNMENT_MOD) & (gwdata->GW_ALIGNMENT - 1));
		if (p == NULL) return (NULL);
		freeable = GWFREEABLE;
	}

/* Initialize the gwnum header */

	q = p + header_size;
	//* (uint32_t *) (q - 8) = size;				/* Size in bytes -- DEPRECATED (if not, gwarray_alloc is broken!) */
	* (uint32_t *) (q - 28) = 0;					/* Has-been-pre-ffted flag */
	* (double *) (q - 16) = 0.0;					/* SUM(INPUTS) */
	* (double *) (q - 24) = 0.0;					/* SUM(OUTPUTS) */
	unnorms (q) = 0.0f;						/* Unnormalized adds count */

/* Do a seemingly pointless memset!  This actually is very important.  The memset will walk through the allocated memory sequentially, which */
/* increases the likelihood that contiguous virtual memory will map to contiguous physical memory.  The FFTs, especially the larger ones, */
/* optimizes L2 cache line collisions on the assumption that the FFT data is in contiguous physical memory.  Failure to do this results in as */
/* much as a 30% performance hit in an SSE2 2M FFT.  I've no idea if this is of any benefit in Linux or more modern Windows or more modern CPUs. */

	memset (q, 0, size);

/* Grow arrays if necessary */

	gwmutex_lock (&gwdata->alloc_lock);			// Obtain lock necessary for thread-safe operation
	if (gwdata->gwnum_alloc_count == gwdata->gwnum_alloc_array_size) {
		int new_gwnum_alloc_array_size = gwdata->gwnum_alloc_array_size + (gwdata->gwnum_alloc_array_size >> 1);
		gwnum *new_gwnum_alloc = (gwnum *) realloc (gwdata->gwnum_alloc, new_gwnum_alloc_array_size * sizeof (gwnum));
		gwnum *new_gwnum_free = (gwnum *) realloc (gwdata->gwnum_free, new_gwnum_alloc_array_size * sizeof (gwnum));
		if (new_gwnum_alloc == NULL || new_gwnum_free == NULL) {
			gwmutex_unlock (&gwdata->alloc_lock);
			if (freeable & GWFREE_LARGE_PAGES) aligned_large_pages_free (p);
			else if (freeable & GWFREEABLE) aligned_free (p);
			return (NULL);
		}
		gwdata->gwnum_alloc = new_gwnum_alloc;
		gwdata->gwnum_free = new_gwnum_free;
		gwdata->gwnum_alloc_array_size = new_gwnum_alloc_array_size;
	}

/* Save pointer for easier cleanup */

	* (int32_t *) (q - 32) = freeable + additional_freeable_flags + gwdata->gwnum_alloc_count;	/* Mem should be freed flag / gwnum_alloc index */
	gwdata->gwnum_alloc[gwdata->gwnum_alloc_count++] = (gwnum) q;
	gwmutex_unlock (&gwdata->alloc_lock);

/* Return the gwnum */

	return ((gwnum) q);
}

/* Allocate memory for a gwnum */

gwnum gwalloc (
	gwhandle *gwdata)
{
	return (real_gwalloc (gwdata, 0));
}

/* Allocate a gwnum used internally (GW_RANDOM, GW_FFT1, etc.).  Such a gwnum will not be freed by gwfreeall. */

gwnum gwalloc_internal (
	gwhandle *gwdata)
{
	return (real_gwalloc (gwdata, GWFREE_INTERNAL));
}

/* Internal routine to really free a gwnum */

void gwfree_freeable (
	gwhandle *gwdata,
	gwnum	q)
{
	gwnum	moved_gwnum;
	int32_t	freeable, moved_freeable, alloc_index;

	// Caller should have also obtained the gwalloc_lock
	ASSERTG (gwdata->clone_of == NULL);

	freeable = * (int32_t *) ((char *) q - 32);
	ASSERTG (freeable & GWFREEABLE);

	// Move the last entry in the gwnum_alloc array to the slot we are about to free
	alloc_index = freeable & GWFREEALLOC_INDEX;
	moved_gwnum = gwdata->gwnum_alloc[--gwdata->gwnum_alloc_count];
	gwdata->gwnum_alloc[alloc_index] = moved_gwnum;
	// Patch the alloc_index of the moved gwnum
	moved_freeable = * (int32_t *) ((char *) moved_gwnum - 32);
	moved_freeable &= ~GWFREEALLOC_INDEX;
	moved_freeable |= alloc_index;
	* (int32_t *) ((char *) moved_gwnum - 32) = moved_freeable;
	// Free the gwnum
	if (freeable & GWFREE_LARGE_PAGES) aligned_large_pages_free ((char *) q - GW_HEADER_SIZE (gwdata));
	else aligned_free ((char *) q - GW_HEADER_SIZE (gwdata));
}

/* Free or cache a gwnum */

void gwfree (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	q)		/* Number to free */
{
	if (q == NULL) return;

/* Feed all frees through the parent gwdata.  Obtain lock necessary for thread-safe operation. */

	if (gwdata->clone_of) gwdata = gwdata->clone_of;
	gwmutex_lock (&gwdata->alloc_lock);

/* When freeing internal memory, remove the GWFREE_INTERNAL flag so the gwnum can be properly added to the free cache */

	* (int32_t *) ((char *) q - 32) &= ~GWFREE_INTERNAL;

/* Free the gwnum if it is freeable and we've cached enough free gwnums */

	ASSERTG (gwdata->gwnum_alloc[* (int32_t *) ((char *) q - 32) & GWFREEALLOC_INDEX] == q);	// Catch attempts to free gwnum within a gwalloc_array
	if (gwdata->gwnum_free_count >= gwdata->gwnum_max_free_count && * (int32_t *) ((char *) q - 32) & GWFREEABLE)
		gwfree_freeable (gwdata, q);

/* Otherwise, cache the gwnum for quicker gwallocs in the future */

	else
		gwdata->gwnum_free[gwdata->gwnum_free_count++] = q;
	gwmutex_unlock (&gwdata->alloc_lock);
}

/* Free cached gwnums */

void gwfree_cached (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	int	i;

/* Feed all frees through the parent gwdata.  Obtain lock necessary for thread-safe operation. */

	if (gwdata->clone_of) gwdata = gwdata->clone_of;
	gwmutex_lock (&gwdata->alloc_lock);

/* Free the cached gwnums that are freeable */

	for (i = 0; i < (int) gwdata->gwnum_free_count; i++) {
		gwnum	q = gwdata->gwnum_free[i];
		int32_t	freeable = * (int32_t *) ((char *) q - 32);
		if (freeable & GWFREEABLE) {
			gwfree_freeable (gwdata, q);
			gwdata->gwnum_free[i--] = gwdata->gwnum_free[--gwdata->gwnum_free_count];
		}
	}
	gwmutex_unlock (&gwdata->alloc_lock);
}

/* Free internal memory that can be safely freed.  Call this prior to using a lot of gwnum memory (this also calls gwfree_cached to further reduce mem used). */

void gwfree_internal_memory (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	// If the gwdata has been cloned, we cannot safely free shared gwnums as the cloned gwdatas may also be using these gwnums
	if (gwdata->clone_count == 0 && gwdata->clone_of == NULL) {
		gwmutex_lock (&gwclone_lock);		// Protect against a clone initializing right now.  The clone would copy pointers we are about to free.
		gwfree (gwdata, gwdata->GW_RANDOM), gwdata->GW_RANDOM = NULL;
		gwfree (gwdata, gwdata->GW_RANDOM_SQUARED), gwdata->GW_RANDOM_SQUARED = NULL;
		gwfree (gwdata, gwdata->GW_RANDOM_FFT), gwdata->GW_RANDOM_FFT = NULL;
		if (gwdata->FFT1_state == 1 && !gwdata->FFT1_user_allocated) gwfree (gwdata, gwdata->GW_FFT1), gwdata->GW_FFT1 = NULL, gwdata->FFT1_state = 0;
		gwmutex_unlock (&gwclone_lock);
	}
	if (gwdata->to_radix_gwdata != NULL) gwdone (gwdata->to_radix_gwdata), free (gwdata->to_radix_gwdata), gwdata->to_radix_gwdata = NULL;
	if (gwdata->from_radix_gwdata != NULL) gwdone (gwdata->from_radix_gwdata), free (gwdata->from_radix_gwdata), gwdata->from_radix_gwdata = NULL;
	gwfree_cached (gwdata);
}

/* Allocate an array and fill it with gwnums.  Uses a little less memory than allocating an array and calling gwalloc many times. */

gwarray gwalloc_array (		/* Pointer to an array of gwnums */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	uint64_t n)		/* Size of the array of gwnums */
{
	size_t	array_header_size, gwnum_header_size, gwnum_size, aligned_gwnum_size, array_size;
	int	pad_frequency, pad_amount, pad_direction;
	char	*p = NULL;			// Pointer to the allocated memory
	int	freeable;			// Flags indicating how memory was allocated

	// Feed all allocates through the parent gwdata
	if (gwdata->clone_of) gwdata = gwdata->clone_of;

	// Calc needed space for each gwnum in the array
	gwnum_header_size = GW_HEADER_SIZE (gwdata);
	gwnum_size = gwnum_datasize (gwdata) + gwnum_header_size;
	aligned_gwnum_size = round_up_to_multiple_of (gwnum_size, 64);

	// For AVX-512 CPUs, rounding to a multiple of 128 bytes seems to be a minor benefit.  More importantly, when polymult reads a single cache line from
	// many gwnums, it is beneficial to have the aligned gwnum size be an odd multiple of the cache line size.  This lets us avoid nasty 4KB strides which
	// make poor use of the CPU caches.  The AVX-512 gwnum FFT size of 12228 is an example where this makes a big difference.
	if (gwdata->cpu_flags & CPU_AVX512F) {
		if (aligned_gwnum_size % 128 == 64) aligned_gwnum_size += 64;
		if (aligned_gwnum_size % 256 == 0) aligned_gwnum_size += 128;
		pad_amount = 128;							// Pad to 128-byte boundaries
	} else {
		if (aligned_gwnum_size % 128 == 0) aligned_gwnum_size += 64;
		pad_amount = 64;							// Pad to 64-byte boundaries
	}

	// Calc needed space for the array_header and array (128 bytes to get first gwnum aligned on a cache line and room for n aligned gwnums)
	array_header_size = sizeof (gwarray_header);
	array_size = (size_t) (array_header_size + n * sizeof (gwnum) + 128 + n * aligned_gwnum_size);

	// If scrambling array pointers, no further padding is necessary
	if (gwdata->scramble_arrays) {
		pad_frequency = 0;
	}
	// Otherwise, when polymult uses two-pass FFTs it accesses cache lines within each gwnum with stride 1 in the second pass and stride 2^some_power in
	// the first pass.  For best cache usage, we want both access patterns to avoid 4KB strides with makes the 2^some_power stride problematic.  The above
	// code insures the stride 1 accesses are optimal.  Add another padding every 64 gwnums (32 gwnums in AVX-512).  This padding will ensure that a
	// stride 64 access pattern can fetch 64 consecutive gwnums before a multiple of 4KB is encountered.  If 2^some_power is more than 64, we another padding
	// every 64*64 gwnums to once again avoid distances that are a multiple of 4KB as much as possible.
	else {
		pad_frequency = 4096 / pad_amount;							// Pad every 4KB (every 32 or 64 gwnums)
		if ((int) (aligned_gwnum_size - gwnum_size) >= pad_amount) pad_direction = -1;		// Undo last padding to avoid multiple of 4KB
		else pad_direction = 1;									// Add a padding to avoid multiple of 4KB
		// Bump array_size for paddings every 32 or 64 gwnums, more paddings every 32*32 or 64*64 gwnums, and so forth.
		array_size += (size_t) (divide_rounding_down (n, pad_frequency) * pad_direction * pad_amount -
					divide_rounding_down (n, pad_frequency * pad_frequency) * pad_direction * pad_amount +
					divide_rounding_down (n, pad_frequency * pad_frequency  * pad_frequency) * pad_direction * pad_amount);
	}

	// Allocate memory using large pages or regular malloc
	if (gwdata->use_large_pages) {
		p = (char *) large_pages_malloc (array_size);
		freeable = GWFREE_LARGE_PAGES;
	}
	// Allocate memory using plain old malloc
	if (p == NULL) {
		p = (char *) malloc (array_size);
		freeable = 0;
	}
	// On failure, return NULL
	if (p == NULL) return (NULL);

	// Create pointers to the array, first gwnum, and array header
	gwarray array = (gwarray) (p + array_header_size);
	array = (gwarray) round_up_to_multiple_of ((intptr_t) array, sizeof (gwnum));
	gwnum next_gwnum = (gwnum) ((char *) array + n * sizeof (gwnum) + gwnum_header_size);
	next_gwnum = (gwnum) round_up_to_multiple_of ((intptr_t) next_gwnum, pad_amount);
	gwarray_header *array_header = (gwarray_header *) ((char *) array - array_header_size);

	// Init the array and each gwnum
	for (int i = 0; i < n; i++) {
		// If extra padding is required, apply that now
		if (pad_frequency && i && i % pad_frequency == 0) {
			// Pad to get off a 4KB multiple
			next_gwnum = (gwnum) ((char *) next_gwnum + pad_direction * pad_amount);
			// Occasionally pad even more so that really large strides also don't get stuck on 4KB multiples
			if (i % (pad_frequency * pad_frequency) == 0) {
				next_gwnum = (gwnum) ((char *) next_gwnum - pad_direction * pad_amount);
				if (i % (pad_frequency * pad_frequency * pad_frequency) == 0) {
					next_gwnum = (gwnum) ((char *) next_gwnum + pad_direction * pad_amount);
				}
			}
		}
		// Set array pointer, clear gwnum header, calc next gwnum pointer assuming no extra padding required
		array[i] = next_gwnum;
		memset ((char *) next_gwnum - gwnum_header_size, 0, gwnum_header_size);
		next_gwnum = (gwnum) ((char *) next_gwnum + aligned_gwnum_size);
	}

	// Clearly we do not fully understand memory accessing.  The above code that makes sure polymult strided accesses have a minimum number of 4KB strides is
	// often slower than simply randomly placing the gwnum pointers in the gwnum array according to our Advanced/Time 8900 synthetic benchmark.  Until we can
	// improve the above code, we default to scrambled addresses.  Unfortunately, real world ECM results show that full scrambling is slower.  Instead, I now
	// scrambling blocks of 64 so that data addresses aren't moved very far.  Real world ECM results show this may be a little faster than no scrambling.
	if (gwdata->scramble_arrays) {
		// Blocks of 64 scramble.  For a full scramble set max_block_size to n.
		uint64_t max_block_size = 64;
		if (gwdata->scramble_arrays == 2) max_block_size = n;					// Hack to allow timing full scrambles
		else if (gwdata->scramble_arrays > 2) max_block_size = gwdata->scramble_arrays;		// Hack to allow timing different block sizes
		for (uint64_t i = 0; i < n; i += max_block_size) {
			uint64_t block_size = (n - i < max_block_size) ? n - i : max_block_size;
			for (uint64_t j = 0; j < block_size; j++) {
				int k = rand () % block_size;
				gwswap (array[i+j], array[i+k]);
			}
		}
	}

	// Obtain lock necessary for thread-safe operation
	gwmutex_lock (&gwdata->alloc_lock);

	// Init array's double-linked list and flags field
	array_header->flags = freeable;			// Flags
	array_header->next = gwdata->array_list;	// Next in linked list of allocated arrays
	array_header->prev = &gwdata->array_list;	// Prev in linked list of allocated arrays
	if (array_header->next != NULL) {		// Patch new next's prev pointer
		gwarray_header *next_header = (gwarray_header *) ((char *) array_header->next - sizeof (gwarray_header));
		next_header->prev = &array_header->next;
	}
	gwdata->array_list = array;			// New head of doubly linked list

	// Unlock and return pointer to array
	gwmutex_unlock (&gwdata->alloc_lock);
	return (array);
}

/* Free a previously allocated array of gwnums */
void gwfree_array (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwarray	array)		/* Array of gwnums to free */
{
	// Return if never allocated
	if (array == NULL) return;

	// Feed all frees through the parent gwdata. Obtain lock necessary for thread-safe operation.
	if (gwdata->clone_of) gwdata = gwdata->clone_of;
	gwmutex_lock (&gwdata->alloc_lock);

	// Remove array from doubly-linked list
	gwarray_header *array_header = (gwarray_header *) ((char *) array - sizeof (gwarray_header));
	*array_header->prev = array_header->next;	// Change prev to point to next
	if (array_header->next != NULL) {		// Change next to point to prev
		gwarray_header *next_header = (gwarray_header *) ((char *) array_header->next - sizeof (gwarray_header));
		next_header->prev = array_header->prev;
	}
	gwmutex_unlock (&gwdata->alloc_lock);

	// Free array
	int freeable = (int) (intptr_t) array[-3];		// Flags
	size_t array_header_size = 3 * sizeof (void *);
	if (freeable & GWFREE_LARGE_PAGES) large_pages_free ((char *) array - array_header_size);
	else free ((char *) array - array_header_size);
}

/* Typeless gwalloc and gwfree routines for giants code to call */

void *gwgiantalloc (
	void	*gwdata)
{
	return ((void *) gwalloc ((gwhandle *) gwdata));
}
void gwgiantfree (
	void	*gwdata,
	void	*q)
{
	gwfree ((gwhandle *) gwdata, (gwnum) q);
}

/* Specialized routine that allows giants code to deallocate one cached gwnum to free up space for allocating FFT giant's sincos data. */

void gwgiantdealloc (
	void	*gwdata_arg)
{
	gwhandle *gwdata = (gwhandle *) gwdata_arg;

	// Feed all frees through the parent gwdata. Obtain lock necessary for thread-safe operation.
	if (gwdata->clone_of) gwdata = gwdata->clone_of;
	gwmutex_lock (&gwdata->alloc_lock);

	// Look for one gwnum to free
	for (unsigned int i = 0; i < gwdata->gwnum_free_count; i++) {
		gwnum q = gwdata->gwnum_free[i];
		int32_t	freeable = * (int32_t *) ((char *) q - 32);
		if (!(freeable & GWFREEABLE)) continue;

		// Remove entry from gwnum_free array
		gwdata->gwnum_free[i] = gwdata->gwnum_free[--gwdata->gwnum_free_count];

		// Really free the gwnum
		gwfree_freeable (gwdata, q);
		break;
	}
	gwmutex_unlock (&gwdata->alloc_lock);
}

/* Free all user allocated numbers */

void gwfreeall (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{

/* Feed all frees through the parent gwdata. Obtain lock necessary for thread-safe operation. */

	if (gwdata->clone_of) gwdata = gwdata->clone_of;
	gwmutex_lock (&gwdata->alloc_lock);

/* Go through the allocated list and free any user allocated gwnums that are freeable. */
/* In other words, unless the user is using the BIGBUF kludge, free all possible memory. */

	gwdata->gwnum_free_count = 0;
	for (unsigned int i = 0; i < gwdata->gwnum_alloc_count; i++) {
		gwnum	q = gwdata->gwnum_alloc[i];
		if (* (int32_t *) ((char *) q - 32) & GWFREE_INTERNAL) continue;
		if (* (int32_t *) ((char *) q - 32) & GWFREEABLE) {
			// Really free the gwnum
			gwfree_freeable (gwdata, q);
			// Compensate for the gwnum that was moved to the current gwnum_alloc slot
			i--;
		}
		else
			gwdata->gwnum_free[gwdata->gwnum_free_count++] = q;
	}

/* Free all allocated arrays too.  Replicate code in gwfree_array.  We cannot call that routine because it would re-obtain the lock. */

	while (gwdata->array_list != NULL) {
		gwarray array = (gwarray) gwdata->array_list;

		// Remove array from doubly-linked list
		gwarray_header *array_header = (gwarray_header *) ((char *) array - sizeof (gwarray_header));
		*array_header->prev = array_header->next;	// Change prev to point to next
		if (array_header->next != NULL) {		// Change next to point to prev
			gwarray_header *next_header = (gwarray_header *) ((char *) array_header->next - sizeof (gwarray_header));
			next_header->prev = array_header->prev;
		}

		// Free array
		int freeable = (int) (intptr_t) array[-3];		// Flags
		size_t array_header_size = 3 * sizeof (void *);
		if (freeable & GWFREE_LARGE_PAGES) large_pages_free ((char *) array - array_header_size);
		else free ((char *) array - array_header_size);
	}

/* Unlock and return */

	gwmutex_unlock (&gwdata->alloc_lock);
}

/* To optimize use of the L1 cache we scramble the FFT data. */
/* Consult the assembly language code for better descriptions of this */
/* shuffling process.  This C code must accurately reflect the shuffling */
/* the assembly language code is expecting. */

unsigned long addr_offset (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long i)
{
	unsigned long fftlen;
	unsigned long addr, i1, i2, i3, i6;

	ASSERTG (i < gwdata->FFTLEN);

	fftlen = gwdata->FFTLEN;

/* Memory layouts for AVX-512 machines */

	if (gwdata->cpu_flags & CPU_AVX512F) {

/* Small FFTs use one pass, not very convoluted.  This example shows low-word/high-word pairs for a length 1280 FFT */
/*	0	8	16	24	32	40	48	56	640	+8	...	*/
/*	1	...										*/
/*	...											*/
/*	7											*/
/*	64											*/
/*	...											*/

		if (gwdata->PASS1_SIZE == 0) {
			unsigned long top_bit, over64;
			top_bit = i / (fftlen >> 1); i -= top_bit * (fftlen >> 1); // 640 in example above
			over64 = i / 64; i -= over64 * 64;
			addr = (over64 << 7) + ((i & 7) << 4) + (top_bit << 3) + (i >> 3);
			addr = addr * sizeof (double);
			/* Now optionally add 64 pad bytes every 4KB */
			if (gwdata->FOURKBGAPSIZE) addr = addr + (addr / (gwdata->FOURKBGAPSIZE << 6)) * 64;
		}

/* Larger FFTs use two passes.  This example shows low-word/high-word pairs for a length 128K FFT (pass2_size = 1K complex or 2K reals) */
/*	0	+1K	+1K	+1K	+1K	+1K	+1K	+1K	64K	+1K	...	*/
/*	8	...										*/
/*	...											*/
/*	1016	...										*/
/*	1	...										*/
/*	...											*/
/*	1017	...										*/
/*	...											*/
/*	7	...										*/
/*	...											*/
/*	1023	...										*/
/*	8K	...										*/
/*	...											*/

		else {
			unsigned long top_bit, grp, row;
			top_bit = i / (fftlen >> 1); i -= top_bit * (fftlen >> 1); // 64K in example above
			i1 = i % gwdata->PASS2_SIZE;	// Element within pass 2, 0-1K in example above
			i = i / gwdata->PASS2_SIZE;	// Pass 1 block number, which 1K block in example above
			grp = (i >> 3) * 8 + (i1 & 7);	// Which 8K block plus which 1-7 block in example above
			row = grp * (gwdata->PASS2_SIZE >> 3) + (i1 >> 3); // Row number, where row is 128-bytes
			addr = (row << 4) + (top_bit << 3) + (i & 7);
			addr = addr * sizeof (double);
			/* Now optionally add bytes for every 4KB page and every pass 2 block */
			addr = addr + (addr >> 12) * gwdata->FOURKBGAPSIZE + grp * gwdata->PASS2GAPSIZE;
		}
	}

/* Memory layouts for AVX machines */

	else if (gwdata->cpu_flags & CPU_AVX) {

/* Small FFTs use one pass, not very convoluted.  This example is for	*/
/* a length 2048 FFT:							*/
/*	0	64	128	192	1024	+64	+64	+64	*/
/*	1	...							*/
/*	...								*/
/*	63								*/
/*	256								*/
/*	...								*/

		if (gwdata->PASS2_SIZE == 0) {
			unsigned long top5bits;
			top5bits = i / (fftlen >> 5); i -= top5bits * (fftlen >> 5);
			addr = ((top5bits >> 2) & 3) * (fftlen >> 2) + (i << 3) + ((top5bits >> 4) << 2) + (top5bits & 3);
			addr = addr * sizeof (double);
			/* Now optionally add 64 pad bytes every 1KB, 2KB or 4KB */
			if (gwdata->FOURKBGAPSIZE)
				addr = addr + (addr / (gwdata->FOURKBGAPSIZE << 6)) * 64;
		}

/* Larger FFTs use two passes.  This example for a length 64K FFT (pass2_size = 1K): */
/*	0	+1K	+1K	+1K	32K	+1K	+1K	+1K	*/
/*	4	...							*/
/*	...								*/
/*	1020	...							*/
/*	1	...							*/
/*	...								*/
/*	1021	...							*/
/*	2	...							*/
/*	...								*/
/*	1022	...							*/
/*	3	...							*/
/*	...								*/
/*	1023	...							*/
/*	4K	...							*/
/*	...								*/

		else {
			unsigned long top_bit, grps, row;
			top_bit = (i << 1) / fftlen; i -= (top_bit * fftlen) >> 1;
			i1 = i % gwdata->PASS2_SIZE; i = i / gwdata->PASS2_SIZE;
			grps = (i >> 2) * 4 + (i1 & 3);
			row = grps * (gwdata->PASS2_SIZE >> 2) + (i1 >> 2);
			addr = (row << 3) + (top_bit << 2) + (i & 3);
			addr = addr * sizeof (double);
			/* Now add 64 bytes every 4KB and one pass2gapsize */
			/* for every pass 2 block. */
			addr = addr + (addr >> 12) * gwdata->FOURKBGAPSIZE + grps * gwdata->PASS2GAPSIZE;
		}
	}

/* Memory layouts for SSE2 machines */

	else if (gwdata->cpu_flags & CPU_SSE2) {
		unsigned long sets, pfa, temp;

/* Small FFTs use one pass, not very convoluted.  This example is for	*/
/* a length 2048 FFT:							*/
/*	0	512	1	513	1024	1536	1025	1537	*/
/*	2	...							*/
/*	...								*/
/*	510								*/
/* PFA-style FFTs are a little tricker.  See assembly code for example.	*/

		if (gwdata->PASS2_SIZE == 0) {
			sets = fftlen >> 3;
			if (i >= (fftlen >> 1)) {
				i6 = 1;
				i -= (fftlen >> 1);
			} else
				i6 = 0;
			i1 = i & 1; i >>= 1;
			i3 = 0;
			for (pfa = sets; pfa > 8; pfa >>= 1);
			if (pfa == 5) {
				temp = sets / 5;
				if (i < temp * 2) {
					sets = temp;
				} else {
					i3 = temp; i -= temp * 2;
					sets = temp * 4;
				}
			} else if (pfa == 7) {
				temp = sets / 7;
				if (i < temp * 2) {
					sets = temp;
				} else if (i < temp * 6) {
					i3 = temp; i -= temp * 2;
					sets = temp * 2;
				} else {
					i3 = temp * 3; i -= temp * 6;
					sets = temp * 4;
				}
			}
			i3 += i % sets; i /= sets;
			addr = (((((i3 << 1) + i6) << 1) + i1) << 1) + i;
			addr = addr * sizeof (double);
		}

/* Larger FFTs use two passes.  This example is for a length 64K FFT (pass2_size = 2K): */
/*	0	1K	16K	17K	32K	33K	48K	49K	*/
/*	1	...							*/
/*	...								*/
/*	1023	...							*/
/*	2K	...							*/
/*	...								*/
/* and PFA layouts are even funkier.					*/

		else if (gwdata->FFT_TYPE == FFT_TYPE_HOME_GROWN) {
			sets = (fftlen / gwdata->PASS2_SIZE) >> 2;
			if (i >= (fftlen >> 1)) {
				i6 = 1;
				i -= (fftlen >> 1);
			} else
				i6 = 0;
			i1 = i % (gwdata->PASS2_SIZE >> 1);
			i = i / (gwdata->PASS2_SIZE >> 1);
			i2 = i & 1; i >>= 1;
			i3 = 0;
			for (pfa = sets; pfa > 8; pfa >>= 1);
			if (pfa == 5) {
				temp = sets / 5;
				if (i < temp * 2) {
					sets = temp;
				} else {
					i3 = temp; i -= temp * 2;
					sets = temp * 4;
				}
			} else if (pfa == 7) {
				temp = sets / 7;
				if (i < temp * 2) {
					sets = temp;
				} else if (i < temp * 6) {
					i3 = temp; i -= temp * 2;
					sets = temp * 2;
				} else {
					i3 = temp * 3; i -= temp * 6;
					sets = temp * 4;
				}
			}
			i3 += i % sets; i /= sets;
			addr = i3 * (gwdata->PASS2_SIZE >> 1);
			addr = ((((((addr + i1) << 1) + i6) << 1) + i) << 1) + i2;
			addr = addr * sizeof (double);
			/* Now add 128 bytes every 8KB and one pass2gapsize */
			/* for every pass 2 block. */
			addr = addr + (addr >> 13) * 128 + i3 * gwdata->PASS2GAPSIZE;
		}

/* Newer traditional radix-4 large FFTs use don't have a special layout for PFA. */

		else {
			unsigned long top2, row;
			top2 = (i << 2) / fftlen; i -= (top2 * fftlen) >> 2;
			i1 = i % (gwdata->PASS2_SIZE >> 1); i = i / (gwdata->PASS2_SIZE >> 1);
			i2 = i & 1; i >>= 1;
			row = i * (gwdata->PASS2_SIZE >> 1) + i1;
			addr = (((row << 2) + top2) << 1) + i2;
			addr = addr * sizeof (double);
			/* Now add 128 bytes every 8KB and one pass2gapsize */
			/* for every pass 2 block. */
			addr = addr + (addr >> 13) * 128 + i * gwdata->PASS2GAPSIZE;
		}
	}

/* One pass x87 FFTs use a near flat memory model. */

	else if (gwdata->PASS2_SIZE == 0) {
		if (i >= (fftlen >> 1)) {
			i2 = 1;
			i -= (fftlen >> 1);
		} else
			i2 = 0;
		addr = i * 16 + i2 * 8;
	}

/* Two pass x87 FFTs use a near flat memory model.  Waste 64 bytes */
/* between 4KB.  Waste 64 bytes between every block (4KB, 16KB, or 64KB). */

	else {
		if (i >= (fftlen >> 1)) {
			i2 = 1;
			i -= (fftlen >> 1);
		} else
			i2 = 0;
		addr = i * 16 + i2 * 8 + (i >> 8) * 64 + (i / gwdata->PASS2_SIZE) * 64;
	}

/* Return the offset */

	return (addr);
}

/* Return the address of ith element in the FFT array */

double *addr (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	g,
	unsigned long i)
{
	return ((double *) ((char *) g + addr_offset (gwdata, i)));
}

// gwiter_next switching constants

#define AVX512_ONE_PASS		0
#define AVX512_TWO_PASS		1
#define AVX_ONE_PASS		2
#define AVX_TWO_PASS		3
#define	SSE2_ONE_PASS		4
#define SSE2_HG_TWO_PASS	5
#define SSE2_TWO_PASS		6
#define X87_ONE_OR_TWO_PASS	7

/* An iterator can be used for faster incrementing through FFT data elements */

void gwiter_init_write_only (	/* Init iterator to gwnum g element zero for writing */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwiter	*iter,		/* Iterator to initialize */
	gwnum	g)		/* gwnum to iterate through */
{
	FFT_state (g) = NOT_FFTed;	// Prevent unfft of g.  We may be recycling a freed gwnum that was FFTed.  Or user may be recycling an FFTed gwnum.
	gwiter_init_zero (gwdata, iter, g);
}

void gwiter_init_zero (		/* Init iterator to element zero */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwiter	*iter,		/* Iterator to initialize */
	gwnum	g)		/* gwnum to iterate through */
{

/* Make sure data is not FFTed.  Caller should really try to avoid this scenario. */

	if (FFT_state (g) != NOT_FFTed) gwunfft (gwdata, g, g);

/* Initialize iterator state */

	if (gwdata->GENERAL_MMGW_MOD) gwdata = gwdata->cyclic_gwdata;
	iter->gwdata = gwdata;
	iter->g = g;
	iter->index = 0;
	gwcached_init_zero (iter->cached_data);
	iter->addr_offset = 0;
	iter->big_word = ((double) gwdata->NUM_B_PER_SMALL_WORD != gwdata->avg_num_b_per_word);

	set_cached_dwpncol_counter (iter->cached_data, 0);		// First partial column is zero
	set_cached_dwpncol_value (iter->cached_data, 0);
	clear_cached_partial_weights (iter->cached_data);		// Flag that cached partial weights are not set

	// Create an index to quickly switch to the appropriate code in gwiter_next
	if (gwdata->cpu_flags & CPU_AVX512F) {
		if (gwdata->PASS1_SIZE == 0) iter->switcher = AVX512_ONE_PASS;
		else iter->switcher = AVX512_TWO_PASS;
	} else if (gwdata->cpu_flags & CPU_AVX) {
		if (gwdata->PASS1_SIZE == 0) iter->switcher = AVX_ONE_PASS;
		else iter->switcher = AVX_TWO_PASS;
	} else if (gwdata->cpu_flags & CPU_SSE2) {
		if (gwdata->PASS2_SIZE == 0) iter->switcher = SSE2_ONE_PASS;
		else if (gwdata->FFT_TYPE == FFT_TYPE_HOME_GROWN) iter->switcher = SSE2_HG_TWO_PASS;
		else iter->switcher = SSE2_TWO_PASS;
	} else 
		iter->switcher = X87_ONE_OR_TWO_PASS;

	// Init constants and counters for next addr_offset calculations
	uint32_t *ao_values = iter->ao_values;
	memset (ao_values, 0, 9 * sizeof (uint32_t));
	switch (iter->switcher) {
	case AVX512_TWO_PASS:
		// Distance one is (usually) PASS2_SIZE/8 128-byte cache lines + pads
		ao_values[0] = gwdata->PASS2_SIZE*16 + (gwdata->PASS2_SIZE >> 8) * gwdata->FOURKBGAPSIZE + gwdata->PASS2GAPSIZE;
		break;
	case AVX_ONE_PASS:
		ao_values[4] = gwdata->FOURKBGAPSIZE ? gwdata->FOURKBGAPSIZE : gwdata->FFTLEN/2/4/4;
		break;
	case AVX_TWO_PASS:
		// Distance one is (usually) PASS2_SIZE/4 64-byte cache lines + pads
		ao_values[0] = gwdata->PASS2_SIZE*16 + (gwdata->PASS2_SIZE >> 8) * gwdata->FOURKBGAPSIZE + gwdata->PASS2GAPSIZE;
		break;
	case SSE2_ONE_PASS: {
		uint32_t num_cache_lines = gwdata->FFTLEN/8;
		uint32_t pfa;
		for (pfa = num_cache_lines; pfa >= 8; pfa >>= 1);	// Top 3 bits gives us the pfa
		if (pfa == 4 || pfa == 6) ao_values[4] = 1, ao_values[5] = num_cache_lines;
		if (pfa == 5) ao_values[4] = 2, ao_values[5] = num_cache_lines/5, ao_values[6] = 4*num_cache_lines/5;
		if (pfa == 7) ao_values[4] = 3, ao_values[5] = num_cache_lines/7, ao_values[6] = 2*num_cache_lines/7, ao_values[7] = 4*num_cache_lines/7;
		break; }
	case SSE2_HG_TWO_PASS: {
		uint32_t num_cache_lines = gwdata->FFTLEN/gwdata->PASS2_SIZE/4;
		uint32_t pfa;
		for (pfa = num_cache_lines; pfa >= 8; pfa >>= 1);	// Top 3 bits gives us the pfa
		if (pfa == 4 || pfa == 6) ao_values[5] = 1, ao_values[6] = num_cache_lines;
		if (pfa == 5) ao_values[5] = 2, ao_values[6] = num_cache_lines/5, ao_values[7] = 4*num_cache_lines/5;
		if (pfa == 7) ao_values[5] = 3, ao_values[6] = num_cache_lines/7, ao_values[7] = 2*num_cache_lines/7, ao_values[8] = 4*num_cache_lines/7;
		break; }
	}
}

void gwiter_init (		/* Init iterator to given element (typically zero) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwiter	*iter,		/* Iterator to initialize */
	gwnum	g,		/* gwnum to iterate through */	  
	uint32_t n)		/* Initial position for the iterator */
{
	ASSERTG (n < gwdata->FFTLEN);

	// Init many of the constants by positioning to element zero
	gwiter_init_zero (gwdata, iter, g); 
	gwdata = iter->gwdata;

	// Init iterator to specified element
	iter->index = n;
	gwcached_init (gwdata->dd_data, iter->cached_data, n);
	iter->addr_offset = addr_offset (gwdata, n);
	iter->big_word = gwcached_is_big_word (gwdata->dd_data, iter->cached_data);

	// Init constants and counters for next addr_offset calculations
	uint32_t *ao_values = iter->ao_values;
	uint32_t ntmp = n;
	switch (iter->switcher) {
	case AVX512_ONE_PASS:
		ao_values[0] = ntmp % 8; ntmp /= 8;
		ao_values[1] = ntmp % 8; ntmp /= 8;
		ao_values[2] = ntmp % (gwdata->FFTLEN/2/8/8);
		break;
	case AVX512_TWO_PASS:
		ao_values[1] = ntmp % 8; ntmp /= 8;
		ao_values[2] = ntmp % (gwdata->PASS2_SIZE/8); ntmp /= (gwdata->PASS2_SIZE/8);
		ao_values[3] = ntmp % 8; ntmp /= 8;
		ao_values[4] = ntmp % (gwdata->FFTLEN/2/(gwdata->PASS2_SIZE*8));
		break;
	case AVX_ONE_PASS:
		ao_values[0] = ntmp % ao_values[4]; ntmp /= ao_values[4];
		ao_values[1] = ntmp % (gwdata->FFTLEN/2/4/4/ao_values[4]); ntmp /= (gwdata->FFTLEN/2/4/4/ao_values[4]);
		ao_values[2] = ntmp % 4; ntmp /= 4;
		ao_values[3] = ntmp % 4;
		break;
	case AVX_TWO_PASS:
		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
			set_cached_dwpncol_counter (iter->cached_data, n % gwdata->PASS2_SIZE);
			set_cached_dwpncol_value (iter->cached_data, dwpn_col (gwdata, n));
			clear_cached_partial_weights (iter->cached_data);		// Flag that cached partial weights are not set
		}
		ao_values[1] = ntmp % 4; ntmp /= 4;
		ao_values[2] = ntmp % (gwdata->PASS2_SIZE/4); ntmp /= (gwdata->PASS2_SIZE/4);
		ao_values[3] = ntmp % 4; ntmp /= 4;
		ao_values[4] = ntmp % (gwdata->FFTLEN/2/(gwdata->PASS2_SIZE*4));
		break;
	case SSE2_ONE_PASS:
		ntmp = ntmp % (gwdata->FFTLEN/2);
		ao_values[0] = ntmp % 2; ntmp /= 2;
		if (ntmp < 2*ao_values[5]) {		// section 0
			ao_values[1] = ntmp % ao_values[5]; ntmp = ntmp / ao_values[5];
			ao_values[3] = 0;
		} else {
			ntmp -= 2*ao_values[5];
			if (ntmp < 2*ao_values[6]) {	// section 1
				ao_values[1] = ntmp % ao_values[6]; ntmp = ntmp / ao_values[6];
				ao_values[3] = 1;
			} else {			// section 2
				ntmp -= 2*ao_values[6];
				ao_values[1] = ntmp % ao_values[7]; ntmp = ntmp / ao_values[7];
				ao_values[3] = 2;
			}
		}
		ao_values[2] = ntmp;
		break;
	case SSE2_HG_TWO_PASS: {
		uint32_t num_cache_lines = gwdata->FFTLEN/gwdata->PASS2_SIZE/4;
		uint32_t pfa;
		for (pfa = num_cache_lines; pfa >= 8; pfa >>= 1);	// Top 3 bits gives us the pfa
		if (pfa == 4 || pfa == 6) ao_values[5] = 1, ao_values[6] = num_cache_lines;
		if (pfa == 5) ao_values[5] = 2, ao_values[6] = num_cache_lines/5, ao_values[7] = 4*num_cache_lines/5;
		if (pfa == 7) ao_values[5] = 3, ao_values[6] = num_cache_lines/7, ao_values[7] = 2*num_cache_lines/7, ao_values[8] = 4*num_cache_lines/7;
		ntmp = ntmp % (gwdata->FFTLEN/2);
		ao_values[0] = ntmp % (gwdata->PASS2_SIZE/2); ntmp /= (gwdata->PASS2_SIZE/2);
		ao_values[1] = ntmp % 2; ntmp /= 2;
		if (ntmp < 2*ao_values[6]) {		// section 0
			ao_values[2] = ntmp % ao_values[6]; ntmp = ntmp / ao_values[6];
			ao_values[4] = 0;
		} else {
			ntmp -= 2*ao_values[6];
			if (ntmp < 2*ao_values[7]) {	// section 1
				ao_values[2] = ntmp % ao_values[7]; ntmp = ntmp / ao_values[7];
				ao_values[4] = 1;
			} else {			// section 2
				ntmp -= 2*ao_values[7];
				ao_values[2] = ntmp % ao_values[8]; ntmp = ntmp / ao_values[8];
				ao_values[4] = 2;
			}
		}
		ao_values[3] = ntmp;
		break; }
	case SSE2_TWO_PASS:
		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
			set_cached_dwpncol_counter (iter->cached_data, n % (gwdata->PASS2_SIZE/2));
			set_cached_dwpncol_value (iter->cached_data, dwpn_col (gwdata, n));
			clear_cached_partial_weights (iter->cached_data);		// Flag that cached partial weights are not set
		}
		ao_values[0] = ntmp % 128; ntmp /= 128;
		ao_values[1] = ntmp % (gwdata->PASS2_SIZE/2/128); ntmp /= (gwdata->PASS2_SIZE/2/128);
		ao_values[2] = ntmp % 2; ntmp /= 2;
		ao_values[3] = ntmp % (gwdata->FFTLEN/4/gwdata->PASS2_SIZE);
		break;
	}
}

void gwiter_next (		/* Position to next element */
	gwiter	*iter)		/* Iterator to initialize */
{
	gwhandle *gwdata = iter->gwdata;	/* Handle initialized by gwsetup */
	ASSERTG (iter->index < gwdata->FFTLEN);

	if (++iter->index < gwdata->FFTLEN) {

		// Update cached values used in optimizing big/little and weight calculations
		gwcached_next (gwdata->dd_data, iter->cached_data, iter->index);
		iter->big_word = gwcached_is_big_word (gwdata->dd_data, iter->cached_data);

		// Determine the next addr_offset from the last addr_offset.  This should be much faster than a call to addr_offset.
		uint32_t *ao_values = iter->ao_values;
		switch (iter->switcher) {
		case AVX512_ONE_PASS:
			iter->addr_offset += 128;
			if (++ao_values[0] == 8) { ao_values[0] = 0; iter->addr_offset -= 8*128;
			iter->addr_offset += 8;
			if (++ao_values[1] == 8) { ao_values[1] = 0; iter->addr_offset -= 8*8;
			iter->addr_offset += 1024 + ((++ao_values[2] & 3) == 0 ? gwdata->FOURKBGAPSIZE : 0);
			if (ao_values[2] == gwdata->FFTLEN/2/8/8) { ao_values[2] = 0; iter->addr_offset = 64;}}}
			break;
		case AVX512_TWO_PASS: {
			uint32_t dist1 = ao_values[0];			// PASS2_SIZE/8 128-byte cache lines + pads
			iter->addr_offset += dist1;
			if (++ao_values[1] == 8) { ao_values[1] = 0; iter->addr_offset -= 8*dist1;
			iter->addr_offset += 128 + ((++ao_values[2] & 31) == 0 ? gwdata->FOURKBGAPSIZE : 0);
			if (ao_values[2] == gwdata->PASS2_SIZE/8) { ao_values[2] = 0; iter->addr_offset -= gwdata->PASS2_SIZE/8*128 + gwdata->PASS2_SIZE/8/32*gwdata->FOURKBGAPSIZE;
			iter->addr_offset += 8;
			if (++ao_values[3] == 8) { ao_values[3] = 0; iter->addr_offset -= 8*8;
			iter->addr_offset += 8*dist1;
			if (++ao_values[4] == gwdata->FFTLEN/2/(gwdata->PASS2_SIZE*8)) { ao_values[4] = 0; iter->addr_offset = 64;}}}}
			break; }
		case AVX_ONE_PASS: {
			uint32_t count1 = ao_values[4];			// gwdata->FOURKBGAPSIZE ? gwdata->FOURKBGAPSIZE : gwdata->FFTLEN/2/4/4;
			iter->addr_offset += 64;
			if (++ao_values[0] == count1) { ao_values[0] = 0;
			iter->addr_offset += 64;			// pad every 1KB, 2KB, or 4KB
			uint32_t count2 = gwdata->FOURKBGAPSIZE ? gwdata->FFTLEN/2/4/4/count1 : 1;
			if (++ao_values[1] == count2) { ao_values[1] = 0; iter->addr_offset -= gwdata->FFTLEN/2/4/4*64 + count2*64;
			iter->addr_offset += 8;
			if (++ao_values[2] == 4) { ao_values[2] = 0; iter->addr_offset -= 4*8;
			iter->addr_offset += gwdata->FFTLEN/2/4/4*64 + (gwdata->FOURKBGAPSIZE ? gwdata->FFTLEN/2/4/4/gwdata->FOURKBGAPSIZE*64 : 0);
			if (++ao_values[3] == 4) { ao_values[2] = 0; iter->addr_offset = 32;}}}}
			break; }
		case AVX_TWO_PASS: {
			// Calculate next dwpncol value used in partial weights
			if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
				inc_cached_dwpncol_counter (iter->cached_data);
				inc_cached_dwpncol_value (iter->cached_data);
				if (get_cached_dwpncol_counter (iter->cached_data) == gwdata->PASS2_SIZE) {
					set_cached_dwpncol_counter (iter->cached_data, 0);
					set_cached_dwpncol_value (iter->cached_data, dwpn_col (gwdata, iter->index));
					clear_cached_partial_weights (iter->cached_data);		// Flag that cached partial weights are not set
				}
			}
			// Now the next addr_offset calculations
			uint32_t dist1 = ao_values[0];			// PASS2_SIZE/4 64-byte cache lines + pads
			iter->addr_offset += dist1;
			if (++ao_values[1] == 4) { ao_values[1] = 0; iter->addr_offset -= 4*dist1;
			iter->addr_offset += 64 + ((++ao_values[2] & 63) == 0 ? gwdata->FOURKBGAPSIZE : 0);
			if (ao_values[2] == gwdata->PASS2_SIZE/4) { ao_values[2] = 0; iter->addr_offset -= gwdata->PASS2_SIZE/4*64 + gwdata->PASS2_SIZE/4/64*gwdata->FOURKBGAPSIZE;
			iter->addr_offset += 8;
			if (++ao_values[3] == 4) { ao_values[3] = 0; iter->addr_offset -= 4*8;
			iter->addr_offset += 4*dist1;
			if (++ao_values[4] == gwdata->FFTLEN/2/(gwdata->PASS2_SIZE*4)) { ao_values[4] = 0; iter->addr_offset = 32;}}}}
			break; }
		case SSE2_ONE_PASS: {
			iter->addr_offset += 16;
			if (++ao_values[0] == 2) { ao_values[0] = 0; iter->addr_offset -= 2*16;
			iter->addr_offset += 64;
			uint32_t pfa_section_number = ao_values[3];
			uint32_t count = ao_values[5+pfa_section_number];
			if (++ao_values[1] == count) { ao_values[1] = 0; iter->addr_offset -= count*64;
			iter->addr_offset += 8;
			if (++ao_values[2] == 2) { ao_values[2] = 0; iter->addr_offset -= 2*8;
			iter->addr_offset += count*64;
			uint32_t num_pfa_sections = ao_values[4];
			if (++ao_values[3] == num_pfa_sections) { ao_values[3] = 0; iter->addr_offset = 32;}}}}
			break; }
		case SSE2_HG_TWO_PASS: {					// Extremely rare?  s.b. eliminated?
			iter->addr_offset += 64 + ((++ao_values[0] & 127) == 0 ? 128 : 0);	// Pad 128 every 8KB
			if (ao_values[0] == gwdata->PASS2_SIZE/2) { ao_values[0] = 0; iter->addr_offset -= gwdata->PASS2_SIZE/2*64 + gwdata->PASS2_SIZE/2/128*128;
			iter->addr_offset += 8;
			if (++ao_values[1] == 2) { ao_values[1] = 0; iter->addr_offset -= 2*8;
			uint32_t dist = gwdata->PASS2_SIZE/2*64 + gwdata->PASS2_SIZE/2/128*128 + gwdata->PASS2GAPSIZE;
			iter->addr_offset += dist;
			uint32_t pfa_section_number = ao_values[4];
			uint32_t count = ao_values[6+pfa_section_number];
			if (++ao_values[2] == count) { ao_values[2] = 0; iter->addr_offset -= count*dist;
			iter->addr_offset += 16;
			if (++ao_values[3] == 2) { ao_values[3] = 0; iter->addr_offset -= 2*16;
			iter->addr_offset += count*dist;
			uint32_t num_pfa_sections = ao_values[5];
			if (++ao_values[4] == num_pfa_sections) { ao_values[4] = 0; iter->addr_offset = 32;}}}}}
			break; }
		case SSE2_TWO_PASS: {
			// Calculate next dwpncol value used in partial weights
			if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
				inc_cached_dwpncol_counter (iter->cached_data);
				inc_cached_dwpncol_value (iter->cached_data);
				if (get_cached_dwpncol_counter (iter->cached_data) == gwdata->PASS2_SIZE / 2) {
					set_cached_dwpncol_counter (iter->cached_data, 0);
					set_cached_dwpncol_value (iter->cached_data, dwpn_col (gwdata, iter->index));
					clear_cached_partial_weights (iter->cached_data);		// Flag that cached partial weights are not set
				}
			}
			// Now the next addr_offset calculations
			ASSERTG ((gwdata->PASS2_SIZE/2) % 128 == 0);
			iter->addr_offset += 64;
			if (++ao_values[0] == 128) { ao_values[0] = 0;
			iter->addr_offset += 128;						// Pad 128 every 8KB
			uint32_t count1 = gwdata->PASS2_SIZE/2/128;
			if (++ao_values[1] == count1) { ao_values[1] = 0; iter->addr_offset -= gwdata->PASS2_SIZE/2*64 + count1*128;
			iter->addr_offset += 8;
			if (++ao_values[2] == 2) { ao_values[2] = 0; iter->addr_offset -= 2*8;
			uint32_t dist = gwdata->PASS2_SIZE/2*64 + gwdata->PASS2_SIZE/2/128*128 + gwdata->PASS2GAPSIZE;
			iter->addr_offset += dist;
			uint32_t count3 = gwdata->FFTLEN/4/gwdata->PASS2_SIZE;
			if (++ao_values[3] == count3) { ao_values[3] = 0; iter->addr_offset -= count3*dist;
			iter->addr_offset += 16;}}}}
			break; }
		case X87_ONE_OR_TWO_PASS:
			// Not bothering to optimize x87 FFTs.  Fall back to a function call.
			iter->addr_offset = addr_offset (gwdata, iter->index);
			break;
		}
	}
}

/* Return the amount of data allocated by gwsetup */

unsigned long gwmemused (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	if (!gwdata->GENERAL_MMGW_MOD) return (gwdata->mem_needed + gwdata->SCRATCH_SIZE);
	return (gwmemused (gwdata->cyclic_gwdata) + gwmemused (gwdata->negacyclic_gwdata));
}

/* Get the amount of memory likely to be allocated for a gwnum.  Looking at the code for aligned_offset_malloc this includes FFT data, header, */
/* a pointer, pad bytes for alignment, and a little bit for malloc overhead. */

unsigned long gwnum_size (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	return (gwnum_datasize (gwdata) + GW_HEADER_SIZE (gwdata) + sizeof (void *) + gwdata->GW_ALIGNMENT + 2 * sizeof (void *));
}

/* Each FFT word is multiplied by a two-to-phi value.  These routines set and get the FFT value without the two-to-phi multiplier. */

int get_fft_value (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	g,
	unsigned long i,
	long	*retval)
{
	double	val, *valaddr;

/* Make sure data is not FFTed.  Caller should really try to avoid this scenario. */

	if (FFT_state (g) != NOT_FFTed) gwunfft (gwdata, g, g);

/* Get the FFT data and validate it */

	valaddr = addr (gwdata, g, i);
	if (! is_valid_double_addr (valaddr)) return (GWERROR_BAD_FFT_DATA);
	val = *valaddr;

/* Rational and AVX-512 FFTs are not weighted */

	if (!gwdata->RATIONAL_FFT && !(gwdata->cpu_flags & CPU_AVX512F)) {

/* Handle r4dwpn FFTs which are only partially normalized */

		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
			val = val * gwfft_partial_weight_inverse_sloppy (gwdata->dd_data, i, dwpn_col (gwdata, i));

/* Multiply by two-to-minus-phi to generate an integer. */

		else
			val = val * gwfft_weight_inverse_sloppy (gwdata->dd_data, i);
	}

/* Round the value to the nearest integer */

	round_double_to_int32 (val, *retval);

/* Return success */

	return (0);
}

int gwiter_get_fft_value (			/* Return unweighted value of FFT data element */
	gwiter	*iter,
	int32_t	*retval)
{
	gwhandle *gwdata = iter->gwdata;	/* Handle initialized by gwsetup */
	gwnum	g = iter->g;
	double	val, *valaddr;

	ASSERTG (FFT_state (g) == NOT_FFTed);
	ASSERTG (iter->index < gwdata->FFTLEN);

/* Get the FFT data and validate it */

	valaddr = gwiter_addr (iter);
	if (! is_valid_double_addr (valaddr)) return (GWERROR_BAD_FFT_DATA);
	val = *valaddr;

/* Rational and AVX-512 FFTs are not weighted */

	if (!gwdata->RATIONAL_FFT && !(gwdata->cpu_flags & CPU_AVX512F)) {

/* Handle r4dwpn FFTs which are only partially normalized */

		if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
			val = val * gwcached_partial_weight_inverse_sloppy (gwdata->dd_data, iter->cached_data, iter->index);

/* Multiply by two-to-minus-phi to generate an integer. */

		else
			val = val * gwcached_weight_inverse_sloppy (gwdata->dd_data, iter->cached_data, iter->index);
	}

/* Round the value to the nearest integer */

	round_double_to_int32 (val, *retval);

/* Return success */

	return (0);
}

void set_fft_value (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	g,
	unsigned long i,
	long	val)
{

/* Handle the rational and AVX-512 FFT case quickly (not weighted) */

	if (gwdata->RATIONAL_FFT || (gwdata->cpu_flags & CPU_AVX512F) || val == 0) {
		* addr (gwdata, g, i) = val;
		return;
	}

/* Handle r4dwpn FFTs which are only partially weighted */

	if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
		* addr (gwdata, g, i) = val * gwfft_partial_weight_sloppy (gwdata->dd_data, i, dwpn_col (gwdata, i));
		return;
	}

/* Multiply by two-to-phi to generate the proper double. */

	* addr (gwdata, g, i) = val * gwfft_weight_sloppy (gwdata->dd_data, i);
}

void gwiter_set_fft_value (		/* Weight and set FFT data element */
	gwiter	*iter,
	int32_t	val)
{
	gwhandle *gwdata = iter->gwdata;
	gwnum	g = iter->g;
	double *valaddr = gwiter_addr (iter);

	ASSERTG (iter->index < gwdata->FFTLEN);

/* Handle the rational and AVX-512 FFT case quickly (not weighted) */

	if (gwdata->RATIONAL_FFT || (gwdata->cpu_flags & CPU_AVX512F) || val == 0)
		*valaddr = val;

/* Handle r4dwpn FFTs which are only partially weighted */

	else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN)
		*valaddr = val * gwcached_partial_weight_sloppy (gwdata->dd_data, iter->cached_data, iter->index);

/* Multiply by two-to-phi to generate the proper double */

	else
		*valaddr = gwcached_weight_sloppy (gwdata->dd_data, iter->cached_data, iter->index, val);
}

/* Some words in the FFT data contain floor(p/N), some words contain */
/* floor(p/N)+1 bits.  This function returns TRUE in the latter case. */

int is_big_word (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long i)
{
	unsigned long base, next_base;

/* Compute the number of b in this word.  It is a big word if the number of b is more than NUM_B_PER_SMALL_WORD. */

	base = gwfft_base (gwdata->dd_data, i);
	next_base = gwfft_base (gwdata->dd_data, i+1);
	return ((next_base - base) > gwdata->NUM_B_PER_SMALL_WORD);
}

/* Routine map a "bit" number into an FFT word and "bit" within that word */
/* If b != 2, this routine locates the nth b amongst the FFT words. */

void bitaddr (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long bit,
	unsigned long *word,
	unsigned long *bit_in_word)
{

/* What word is the bit in? */

	*word = (unsigned long) ((double) bit / gwdata->avg_num_b_per_word);
	if (*word >= gwdata->FFTLEN) *word = gwdata->FFTLEN - 1;

/* Catch an extreme edge case.  For example: n = 333043493, FFTlen = 18874368 has avg_num_b_per_word of 17.645279195573593.  Bit = 312719184 needs */
/* more than 53 bits of precision to conclude that *word of 17722540.999999996997389166825727 must be rounded down to 17722540. */

	unsigned long base = gwfft_base (gwdata->dd_data, *word);
	if (base > bit) *word -= 1, base = gwfft_base (gwdata->dd_data, *word);

/* Compute the bit within the word. */

	*bit_in_word = bit - base;
}

/* Map a gwerror code into human readable text */

void gwerror_text (
	gwhandle *gwdata,	/* Handle used in all gwnum calls */
	int	error_code,	/* Error code to turn ino text */
	char	*buf,		/* Buffer to write text to */
	int	buflen)		/* Sizeof the text buffer */
{
	char	localbuf[512];

/* Map error code to text */

	switch (error_code) {
	case GWERROR_VERSION:
		strcpy (localbuf, "Improperly compiled and linked.  Gwnum.h and FFT assembly code version numbers do not match.");
		break;
	case GWERROR_TOO_SMALL:
		strcpy (localbuf, "Number sent to gwsetup is less than or equal to one.");
		break;
	case GWERROR_TOO_LARGE:
		strcpy (localbuf, "Number sent to gwsetup is too large for the FFTs to handle.");
		break;
	case GWERROR_K_TOO_SMALL:
		strcpy (localbuf, "Value of k in k*b^n+c is too small.  Values less than one are not supported.");
		break;
	case GWERROR_K_TOO_LARGE:
		strcpy (localbuf, "Value of k in k*b^n+c is too large.  Values greater than 2251799813685247 are not supported.");
		break;
	case GWERROR_MALLOC:
		strcpy (localbuf, "Unable to allocate memory.  One possible cause is the operating system's swap area is too small.");
		break;
	case GWERROR_VERSION_MISMATCH:
		strcpy (localbuf, "GWNUM_VERSION from gwinit call doesn't match GWNUM_VERSION when gwnum.c was compiled.  Recompile and relink.");
		break;
	case GWERROR_STRUCT_SIZE_MISMATCH:
		strcpy (localbuf, "Gwhandle structure size from gwinit call doesn't match size when gwnum.c was compiled.  Check compiler alignment switches, recompile and relink.");
		break;
	case GWERROR_BAD_FFT_DATA:
		strcpy (localbuf, "Gwnum had corrupted data such as Nan or Inf.");
		break;
	default:
		if (error_code >= GWERROR_INTERNAL && error_code <= GWERROR_INTERNAL+100)
			sprintf (localbuf, "Internal error #%d.  Please contact the program's author.", error_code - GWERROR_INTERNAL);
		else
			sprintf (localbuf, "Unknown gwnum error code: %d", error_code);
		break;
	}

/* Copy error message to caller's buffer */

	if ((int) strlen (localbuf) >= buflen) localbuf[buflen-1] = 0;
	strcpy (buf, localbuf);
}

/* Return a description of the FFT type chosen */

void gwfft_description (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	char	*buf)		/* Buffer to return string in */
{
	char	*arch, *ffttype;

	gwhandle *fft_gwdata = (gwdata->GENERAL_MMGW_MOD ? gwdata->cyclic_gwdata : gwdata);

	arch = "";
	if (fft_gwdata->cpu_flags & CPU_AVX512F) {
		// No output means Intel Skylake-X or Blend optimized
	}
	else if (fft_gwdata->cpu_flags & CPU_AVX) {
		// No output means Intel Core2 or FMA3 or Blend optimized
	}
	else if (fft_gwdata->cpu_flags & CPU_SSE2) {
		// No output means Intel Core2 or Blend optimized
		if (fft_gwdata->ARCH == ARCH_P4 || fft_gwdata->ARCH == ARCH_P4TP) arch = "Pentium4 ";
		if (fft_gwdata->ARCH == ARCH_K8) arch = "AMD K8 ";
		if (fft_gwdata->ARCH == ARCH_K10) arch = "AMD K10 ";
	}

	ffttype = "";
	if (fft_gwdata->PASS2_SIZE) {
		if (fft_gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX)) {
			// No output means r4dwpn (type-3) FFT
		}
		else if (fft_gwdata->cpu_flags & CPU_SSE2) {
			// No output means r4dwpn (type-3) FFT
			if (fft_gwdata->FFT_TYPE == 0) ffttype = "type-0 ";
			if (fft_gwdata->FFT_TYPE == 1) ffttype = "type-1 ";
			if (fft_gwdata->FFT_TYPE == 2) ffttype = "type-2 ";
		}
	}

	sprintf (buf, "%s%s%s%s%sFFT length %s%lu%s",
		 gwdata->GENERAL_MOD ? "Barrett reduction " :
		 gwdata->GENERAL_MMGW_MOD ? "Montgomery reduction " : "",
		 fft_gwdata->NEGACYCLIC_FFT ? "negacyclic " :
		 fft_gwdata->ZERO_PADDED_FFT ? "zero-padded " : "",
		 (fft_gwdata->cpu_flags & CPU_AVX512F) ? "AVX-512 " :
		 (fft_gwdata->ARCH == ARCH_FMA3) ? "FMA3 " :
		 (fft_gwdata->cpu_flags & CPU_AVX) ? "AVX " :
		 (fft_gwdata->cpu_flags & CPU_SSE2) ? "" : "x87 ",
		 arch, ffttype,
		 gwdata->GENERAL_MMGW_MOD ? "2x" : "",
		 fft_gwdata->FFTLEN >= 1048576 && (fft_gwdata->FFTLEN & 0xFFFFF) == 0 ? fft_gwdata->FFTLEN / 1048576 :
		 fft_gwdata->FFTLEN >= 1024 && (fft_gwdata->FFTLEN & 0x3FF) == 0 ? fft_gwdata->FFTLEN / 1024 : fft_gwdata->FFTLEN,
		 fft_gwdata->FFTLEN >= 1048576 && (fft_gwdata->FFTLEN & 0xFFFFF) == 0 ? "M" :
		 fft_gwdata->FFTLEN >= 1024 && (fft_gwdata->FFTLEN & 0x3FF) == 0 ? "K" : "");

	if (fft_gwdata->PASS1_SIZE) {
		int	clm;
		char	p1buf[20], p2buf[20];

		if (fft_gwdata->cpu_flags & CPU_AVX512F) clm = fft_gwdata->PASS1_CACHE_LINES / 8;
		else if (fft_gwdata->cpu_flags & CPU_AVX) clm = fft_gwdata->PASS1_CACHE_LINES / 4;
		else clm = fft_gwdata->PASS1_CACHE_LINES / 2;
		if (fft_gwdata->PASS1_SIZE % 1024) sprintf (p1buf, "%d", (int) fft_gwdata->PASS1_SIZE);
		else sprintf (p1buf, "%dK", (int) (fft_gwdata->PASS1_SIZE / 1024));
		if (fft_gwdata->PASS2_SIZE % 1024) sprintf (p2buf, "%d", (int) fft_gwdata->PASS2_SIZE);
		else sprintf (p2buf, "%dK", (int) (fft_gwdata->PASS2_SIZE / 1024));
		sprintf (buf + strlen (buf), ", Pass1=%s, Pass2=%s, clm=%d", p1buf, p2buf, clm);
	}

	if (fft_gwdata->num_threads > 1)
		sprintf (buf + strlen (buf), ", %d threads", (int) fft_gwdata->num_threads);

	if (gw_using_large_pages (gwdata))
		strcat (buf, " using large pages");
}

/* Return a string representation of a k/b/n/c combination */

void gw_as_string (
	char	*buf,		/* Buffer to return string in */
	double	k,		/* K in K*B^N+C */
	unsigned long b,	/* B in K*B^N+C */
	unsigned long n,	/* N in K*B^N+C */
	signed long c)		/* C in K*B^N+C */
{
	if (n == 0)
		sprintf (buf, "%.0f", k + c);
	else if (k != 1.0)
		sprintf (buf, "%.0f*%lu^%lu%c%lu", k, b, n, c < 0 ? '-' : '+', (unsigned long) labs (c));
	else if (b == 2 && c == -1)
		sprintf (buf, "M%lu", n);
	else {
		unsigned long cnt, temp_n;
		for (cnt = 0, temp_n = n; !(temp_n & 1); temp_n >>= 1, cnt++);
		if (b == 2 && temp_n == 1 && c == 1)
			sprintf (buf, "F%lu", cnt);
		else
			sprintf (buf, "%lu^%lu%c%lu", b, n, c < 0 ? '-' : '+', (unsigned long) labs (c));
	}
}

/* Enable, disable, get or clear the roundoff error.  Remember that if the roundoff error exceeds 0.5 then the FFT results will be wrong. */
/* It is prudent to watch the roundoff error to make sure the roundoff error does not get close to 0.5, especially if near the limit of an an FFT length. */

void gwerror_checking (
	gwhandle *gwdata,
	int	e)		/* True or false for roundoff error checking on/off */
{
	if (gwdata->GENERAL_MMGW_MOD) {
		gwerror_checking (gwdata->cyclic_gwdata, e);
		gwerror_checking (gwdata->negacyclic_gwdata, e);
	}
	gwdata->ERROR_CHECKING = e ? 1 : 0;
}
double gw_get_maxerr (
	gwhandle *gwdata)
{
	if (!gwdata->GENERAL_MMGW_MOD) return (((struct gwasm_data *) gwdata->asm_data)->MAXERR);
	double err1 = gw_get_maxerr (gwdata->cyclic_gwdata);
	double err2 = gw_get_maxerr (gwdata->negacyclic_gwdata);
	return (err1 > err2 ? err1 : err2);
}
void gw_clear_maxerr (
	gwhandle *gwdata)
{
	if (!gwdata->GENERAL_MMGW_MOD) ((struct gwasm_data *) gwdata->asm_data)->MAXERR = 0.0;
	else gw_clear_maxerr (gwdata->cyclic_gwdata), gw_clear_maxerr (gwdata->negacyclic_gwdata);
}

/* Get or clear the fft count */

uint64_t gw_get_fft_count (
	gwhandle *gwdata)
{
	if (gwdata->GENERAL_MMGW_MOD) return (gw_get_fft_count (gwdata->cyclic_gwdata) + gw_get_fft_count (gwdata->negacyclic_gwdata));
	return (gwdata->fft_count);
}
void gw_clear_fft_count (
	gwhandle *gwdata)
{
	if (gwdata->GENERAL_MMGW_MOD) {
		gw_clear_fft_count (gwdata->cyclic_gwdata);
		gw_clear_fft_count (gwdata->negacyclic_gwdata);
	} else
		gwdata->fft_count = 0;
}

/* Return TRUE if we are operating near the limit of this FFT length.  Input argument is the percentage to consider as near the limit. */
/* For example, if percent is 0.1 and the FFT can handle 20 bits per word, then if there are more than 19.98 bits per word this function will return TRUE. */

int gwnear_fft_limit (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	pct)
{

/* Return TRUE if the virtual bits per word is near the maximum bits per word. */

	if (!gwdata->GENERAL_MMGW_MOD) return (virtual_bits_per_word (gwdata) > (100.0 - pct) / 100.0 * gwdata->fft_max_bits_per_word);
	return (gwnear_fft_limit (gwdata->negacyclic_gwdata, pct));
}

/* Compute the virtual bits per word.  That is, the mersenne-mod-equivalent bits that this k,b,c combination uses. */
/* This code inverts the calculations gwinfo uses in determining whether a k,b,n,c combination will work for a given FFT size. */
/* We have some concerns that we should err on the high side now that version 30.4 is more aggressive in converting gwadd into gwaddquickly. */

double virtual_bits_per_word (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	double	log2b, b_per_input_word, weighted_bits_per_output_word;
	double	max_weighted_bits_per_output_word;
	int	num_b_in_big_word, num_small_words, num_big_words;

	log2b = log2 (gwdata->b);

/* Compute our bits per output word exactly like gwinfo does for a zero padded FFT. */

	if (gwdata->ZERO_PADDED_FFT) {
		b_per_input_word = (double) (gwdata->n + gwdata->n) / gwdata->FFTLEN;
		num_b_in_big_word = (int) ceil (b_per_input_word);
		num_small_words = (int) ((num_b_in_big_word - b_per_input_word) * (gwdata->FFTLEN / 2 + 4));
		num_big_words = (gwdata->FFTLEN / 2 + 4) - num_small_words;
		max_weighted_bits_per_output_word =
			2.0 * gwdata->fft_max_bits_per_word + 0.6 * log2 (gwdata->FFTLEN / 2 + 4);
		weighted_bits_per_output_word =
		       2.0 * ((b_per_input_word + 1.0) * log2b - 1.0) +
		       0.6 * log2 (gwdata->FFTLEN / 2 + 4);
		if ((gwdata->n + gwdata->n) % gwdata->FFTLEN == 0)
			weighted_bits_per_output_word -= ((log2b <= 4.0) ? log2b : 1.4 * log2b);
		else if (! is_pathological_distribution (num_big_words, num_small_words))
			weighted_bits_per_output_word -=
				((log2b <= 3.0) ? (log2b - 1.0) / 2.0 :
				 (log2b <= 6.0) ? 1.0 + (log2b - 3.0) / 3.0 :
						  2.0 + (log2b - 6.0) / 6.0);
	}

/* Compute our bits per output word exactly like gwinfo does for a non-zero-padded FFT. */

	else {
		b_per_input_word = gwdata->avg_num_b_per_word;
		num_b_in_big_word = (int) ceil (b_per_input_word);
		num_small_words = (int) ((num_b_in_big_word - b_per_input_word) * gwdata->FFTLEN);
		num_big_words = gwdata->FFTLEN - num_small_words;
		max_weighted_bits_per_output_word = 2.0 * gwdata->fft_max_bits_per_word + 0.6 * log2 (gwdata->FFTLEN);
		weighted_bits_per_output_word =
			2.0 * ((b_per_input_word + 1.0) * log2b - 1.0) +
			0.6 * log2 (gwdata->FFTLEN) +
			log2 (gwdata->k) + 1.7 * log2 (labs (gwdata->c));
		if (gwdata->k == 1.0 && gwdata->n % gwdata->FFTLEN == 0)
			weighted_bits_per_output_word -= ((log2b <= 4.0) ? log2b : 1.4 * log2b);
		else if (num_big_words == 1 && gwdata->k > 1.0)
			weighted_bits_per_output_word += log2b;
		else if (! is_pathological_distribution (num_big_words, num_small_words))
			weighted_bits_per_output_word -=
				((log2b <= 3.0) ? (log2b - 1.0) / 2.0 :
				 (log2b <= 6.0) ? 1.0 + (log2b - 3.0) / 3.0 :
						  2.0 + (log2b - 6.0) / 6.0);
	}

/* Now generate a value that can be compared to gwdata->fft_max_bits_per_word */

	return (gwdata->fft_max_bits_per_word - (max_weighted_bits_per_output_word - weighted_bits_per_output_word) / 2.0);
}

/* Set the safety margin for upcoming polymult operations */
void gwset_polymult_safety_margin (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	float	safetyval)	/* Safety margin required for polymult operations */
{
	gwdata->polymult_safety_margin = safetyval;
	gwdata->EXTRA_BITS = (float) ((gwdata->fft_max_bits_per_word - gwdata->safety_margin - gwdata->polymult_safety_margin - virtual_bits_per_word (gwdata)) * 2.0);
	if (gwdata->EXTRA_BITS < 0.0f) gwdata->EXTRA_BITS = 0.0f;
	gwdata->EXTRA_BITS += EB_GWMUL_SAVINGS;
}

/* Given k,b,n,c determine the fft length.  If k,b,n,c is not supported */
/* then return zero.  Does not use benchmarking data. */

unsigned long gwmap_to_fftlen (
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	return (gwmap_with_cpu_flags_to_fftlen (0, k, b, n, c));
}

unsigned long gwmap_with_cpu_flags_to_fftlen (
	int	cpu_flags,
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	gwhandle gwdata;	/* Temporary gwnum handle */

/* Get pointer to fft info and return the FFT length */

	gwinit (&gwdata);
	if (cpu_flags) gwdata.cpu_flags = cpu_flags;
	gwclear_use_benchmarks (&gwdata);
	if (gwinfo (&gwdata, k, b, n, c)) return (0);
	return (gwdata.jmptab->fftlen);
}

/* Given an fft length, determine the maximum allowable exponent.  If fftlen */
/* is not supported then return zero.  Does not use benchmarking data. */

unsigned long gwmap_fftlen_to_max_exponent (
	unsigned long fftlen)
{
	return (gwmap_with_cpu_flags_fftlen_to_max_exponent (0, fftlen));
}

unsigned long gwmap_with_cpu_flags_fftlen_to_max_exponent (
	int	cpu_flags,
	unsigned long fftlen)
{
	gwhandle gwdata;	/* Temporary gwnum handle */

/* Get pointer to fft info and return the maximum exponent for the FFT length */

	gwinit (&gwdata);
	if (cpu_flags) gwdata.cpu_flags = cpu_flags;
	gwclear_use_benchmarks (&gwdata);
	gwset_minimum_fftlen (&gwdata, fftlen);
	if (gwinfo (&gwdata, 1.0, 2, 0, -1)) return (0);
	return (adjusted_max_exponent (&gwdata, gwdata.jmptab));
}

/* Given an fft length, determine how much memory is used for normalization */
/* and sin/cos tables.  If k,b,n,c is not supported, then kludgily return */
/* 100 million bytes used.  Does not use benchmarking data. */

unsigned long gwmap_to_memused (
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	gwhandle gwdata;	/* Temporary gwnum handle */

/* Get pointer to fft info and return the memory used */

	gwinit (&gwdata);
	gwclear_use_benchmarks (&gwdata);
	if (gwinfo (&gwdata, k, b, n, c)) return (100000000L);
	return (gwdata.mem_needed + gwdata.SCRATCH_SIZE);
}

/* Return the estimated size of a gwnum.  Does not use benchmarking data. */

unsigned long gwmap_to_estimated_size (
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	gwhandle gwdata;	/* Temporary gwnum handle */

/* Get pointer to fft info and return the memory used */

	gwinit (&gwdata);
	gwclear_use_benchmarks (&gwdata);
	if (gwinfo (&gwdata, k, b, n, c)) return (100000000L);
	return (gwnum_datasize (&gwdata));
}

/* Speed of other x87 processors compared to a Pentium II */

#define REL_486_SPEED	8.4	/* 486 is over 8 times slower than PII */
#define REL_K6_SPEED	3.0	/* K6 is 3 times slower than PII */
#define REL_P3_SPEED	0.8	/* Pentium III is 20% faster than PII */
#define REL_K7_SPEED	0.6	/* Athlons are much faster than a PII */

/* Speed of other SSE2 processors compared to a Pentium 4 */

#define REL_AMD64_SPEED	1.1	/* AMD64 is slightly slower than a P4 */
#define REL_PM_SPEED	1.4	/* Pentium M, Core are much slower than a P4 */
#define REL_ATOM_SPEED	5.0	/* Atoms are much, much slower than a P4 */
#define REL_CORE2_SPEED	0.625	/* Core 2 is much faster than a P4 */
#define REL_I7_SPEED	0.59	/* Core i7 is even faster than a Core 2 */
#define REL_PHENOM_SPEED 0.67	/* AMD Phenom is faster that a P4 */
#define REL_FMA3_SPEED	0.95	/* Haswell FMA3 is faster than a Ivy/Sandy Bridge AVX CPU */

/* Speed of other AVX processors compared to a Sandy Bridge */

#define REL_BULLDOZER_SPEED	1.9	/* Bulldozer is slower than Sandy Bridge */
#define REL_ZEN_SPEED		0.8	/* Zen is better than Haswell */

/* Make a guess as to how long a squaring will take.  If the number cannot */
/* be handled, then kludgily return 100.0.  Does not use benchmarking data. */

double gwmap_to_timing (
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
{
	gwhandle gwdata;	/* Temporary gwnum handle */
	double	timing;

/* Get pointer to fft info */

	gwinit (&gwdata);
	gwclear_use_benchmarks (&gwdata);
	if (gwinfo (&gwdata, k, b, n, c)) return (100.0);

/* Use my PII-400 or P4-1400 timings as a guide. */

	timing = gwdata.jmptab->timing;

/* Since the program is about 10% memory bound, the program will not */
/* speed up linearly with increase in chip speed.  Note, no attempt is */
/* made to differentiate between memory bus speed - we're */
/* just returning an educated guess here. */

/* Adjust timing for various CPU architectures. */
/* For Intel, 486s were very slow.  Pentium, Pentium Pro, Pentium II, */
/* and old celerons were slow because they did not support prefetch. */
/* AMD64s and Pentium Ms are slower than P4s. */

	if (gwdata.cpu_flags & CPU_AVX512F) {
		timing = 0.10 * timing + 0.90 * timing * 4100.0 / CPU_SPEED;	/* Calibrated for Skylake */	//BUG - not timed yet
		if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_OTHER) timing *= REL_ZEN_SPEED;  /* Complete guess for future AMD CPUs */
	} else if (gwdata.cpu_flags & CPU_AVX) {
		timing = 0.10 * timing + 0.90 * timing * 4100.0 / CPU_SPEED;	/* Calibrated for Sandy Bridge */
		if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_OTHER) timing *= REL_ZEN_SPEED;  /* Complete guess for future AMD CPUs */
		else if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_ZEN) timing *= REL_ZEN_SPEED;
		else if (strstr (CPU_BRAND, "AMD")) timing *= REL_BULLDOZER_SPEED;
		if (gwdata.cpu_flags & CPU_FMA3) timing *= REL_FMA3_SPEED;
	} else if (gwdata.cpu_flags & CPU_SSE2) {
		timing = 0.10 * timing + 0.90 * timing * 1400.0 / CPU_SPEED;
		if (strstr (CPU_BRAND, "Phenom")) timing *= REL_PHENOM_SPEED;
		else if (strstr (CPU_BRAND, "AMD")) timing *= REL_AMD64_SPEED;
		else if (strstr (CPU_BRAND, "Atom")) timing *= REL_ATOM_SPEED;
		else if (strstr (CPU_BRAND, "Core 2")) timing *= REL_CORE2_SPEED;
		else if (strstr (CPU_BRAND, "Core(TM)2")) timing *= REL_CORE2_SPEED;
		else if (strstr (CPU_BRAND, "Core(TM) i7")) timing *= REL_I7_SPEED;
		else if (strstr (CPU_BRAND, "Pentium(R) M")) timing *= REL_PM_SPEED;
		else if (strstr (CPU_BRAND, "Core")) timing *= REL_PM_SPEED;
	} else {
		timing = 0.10 * timing + 0.90 * timing * 400.0 / CPU_SPEED;
		if (strstr (CPU_BRAND, "486")) timing *= REL_486_SPEED;
		else if (strstr (CPU_BRAND, "Intel")) {
			if (gwdata.cpu_flags & CPU_PREFETCH) timing *= REL_P3_SPEED;
		} else if (strstr (CPU_BRAND, "AMD")) {
			if (strstr (CPU_BRAND, "Unknown")) timing *= REL_486_SPEED;
			else if (strstr (CPU_BRAND, "K5")) timing *= REL_486_SPEED;
			else if (strstr (CPU_BRAND, "K6")) timing *= REL_K6_SPEED;
			else timing *= REL_K7_SPEED;
		} else
			timing *= REL_486_SPEED;
	}
	return (timing);
}

/* Given k,b,n,c determine the fft length and zero-padding state to be */
/* used.  Caller peers into the gwdata structure to get this info. */
// DEPRECATED
//int gwmap_to_fft_info (
//	gwhandle *gwdata,	/* Uninitialized gwnum global data */
//	double	k,		/* K in K*B^N+C. Must be a positive integer. */
//	unsigned long b,	/* B in K*B^N+C. */
//	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
//	signed long c)		/* C in K*B^N+C. Must be rel. prime to K. */
//{
//	gwinit (gwdata);
//	return (gwinfo (gwdata, k, b, n, c));
//}


/* Internal routine to help gwcopyzero */

void calc8ptrs (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long n,	/* Number of words to zero */
	uint32_t *ptrs)
{
	unsigned long i, j, k;

/* AVX-512F processes one double cache line at a time.  The one-pass and two-pass mameory layouts are different. */

	if (gwdata->cpu_flags & CPU_AVX512F) {
		int	block_size, upper_avx512_word;

/* First the one-pass case.  The copyzero array contains masks and counts */
/* for when to load new masks.  Here is an example for a 2K FFT (layout below) */
/*	0	+8	+8	+8	+8	+8	+8	+8	1K	+8	...	*/
/*	1	...										*/
/*	...											*/
/*	7	...										*/
/*	64	...										*/
/*	...											*/
/* The assembly code processes 1 block at a time.  Block 0 processes 0 to 63 and 1K to 1K+63. */
/* Block 1 processes 64 to 127 and 1K+64 to 1K+127, etc.  If n = 64 + 18, then the masks in block 0 are */
/* 00000000, 11111111, count = inf (zero low words, copy high words).  The masks in block 1 are */
/* 11111000, 11111111, count = 2, 11111100, 11111111, count = inf.  The masks in blocks 2+ are */
/* 11111111, 11111111, count = inf (copy low and high words). */

/* The two-pass case is a little more complex. Here is an example for a 128K FFT (layout below) */
/*	0	+1K	+1K	+1K	+1K	+1K	+1K	+1K	64K	+1K	...	*/
/*	8	...										*/
/*	...											*/
/*	1016	...										*/
/*	1	...										*/
/*	...											*/
/*	1017	...										*/
/*	...											*/
/*	7	...										*/
/*	...											*/
/*	1023	...										*/
/*	8K	...										*/
/* The assembly code processes 1 block at a time.  Block 0 processes 0 to 8K-1 and 64K to 72K-1. */
/* Block 1 processes 8K to 16K-1 and 72K to 80K-1, etc.  If n = 9K + 20, then the masks in block 0 are */
/* 00000000, 11111111, count = inf (zero low words, copy high words).  The masks in block 1 are */
/* 11111100, 11111111, count = 20, 11111110, 11111111, count = inf.  The masks in blocks 2+ are */
/* 11111111, 11111111, count = inf (copy low and high words). */

		if (gwdata->PASS1_SIZE == 0) {
			upper_avx512_word = 8;
			block_size = 64;
		} else {
			upper_avx512_word = gwdata->PASS2_SIZE;
			block_size = upper_avx512_word * 8;
		}

		// Create masks assuming n is below the half way point, we correct for this later if not true
		// Masks and count for the "processing block before n" case.
		ptrs[0] = 0xFF00;
		ptrs[1] = 0xFFFFFFFF;
		// Masks and count for the "processing block containing n" case.
		i = n % block_size;				// Offset within block (0 to 63 or 8K-1 in examples above)
		j = i / upper_avx512_word;			// Column number (which multiple of 8 or 1K in examples above)
		ptrs[2] = 0xFFFF << (j+1);			// Zero j+1 columns mask
		ptrs[3] = i - j * upper_avx512_word;		// Count
		ptrs[4] = 0xFFFF << j;				// Zero j columns mask
		ptrs[5] = 0xFFFFFFFF;				// Count
		// Masks and count for the "processing block after n" case.
		ptrs[6] = 0xFFFF;
		ptrs[7] = 0xFFFFFFFF;
		/* If n > half way point, change masks to always zero low words */
		if (n >= gwdata->FFTLEN/2) ptrs[0] <<= 8, ptrs[2] <<= 8, ptrs[4] <<= 8, ptrs[6] <<= 8;
		/* Assembly code does not like first count of zero, compensate */
		if (ptrs[3] == 0) ptrs[2] = ptrs[4], ptrs[3] = ptrs[5];
	}

/* This is a grossly inefficient way to do this.  However, it should be called rarely. */

/* AVX offsets do not always increase, use a new method.  Remember the */
/* offset of the first word in each column that doesn't need zeroing. */

	else if (gwdata->cpu_flags & CPU_AVX) {
		for (i = 0; i < 8; i++) ptrs[i] = 0xFFFFFFFF;	/* Start by clearing the eight ptrs. */
		for (i = n; i < gwdata->FFTLEN; i++) {
			j = addr_offset (gwdata, i);
			k = (j & 63) >> 3;
			if (ptrs[k] == 0xFFFFFFFF) ptrs[k] = j - (k << 3);
		}
	}

/* The original version that works for offsets that are always increasing */

	else {
		for (i = 0; i < 8; i++) ptrs[i] = 0;	/* Start by clearing the eight ptrs. */
		for (i = 0; i < n; i++) {
			j = addr_offset (gwdata, i);
			k = (j & 63) >> 3;
			if (j >= ptrs[k]) ptrs[k] = j - (k << 3) + 64;
		}
	}
}

/* Routine that sets up and calls assembly code to copy a gwnum from */
/* source to dest while zeroing some lower FFT words */

void gwcopyzero (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,
	gwnum	d,
	unsigned long n)
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;

	ASSERTG (FFT_state (s) == NOT_FFTed);		// Number not FFTed

/* Handle case where no words are zeroed.  Some of the assembly routines */
/* do not like a word count of zero. */

	if (n == 0) {
		gwcopy (gwdata, s, d);
		return;
	}

/* Calculate pointers for ASM routines to do their zero-copy */

	if (n != gwdata->saved_copyz_n) {
		if (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2))
			calc8ptrs (gwdata, n, asm_data->COPYZERO);
		gwdata->saved_copyz_n = n;
	}

/* Do an AVX-512 FFT copyzero one block at a time */
/* This is in preparation for multi-thread support */

	if (gwdata->cpu_flags & CPU_AVX512F) {
		//BUG - multithread this?? support AVX too?? change asm to process more than 16 doubles at a time?
		int	i, num_blks, blk_containing_n;

		if (gwdata->PASS1_SIZE == 0)
			blk_containing_n = (n % (gwdata->FFTLEN / 2)) / 64;
		else
			blk_containing_n = (n % (gwdata->FFTLEN / 2)) / (gwdata->PASS2_SIZE * 8);
		num_blks = asm_data->addcount1;
		for (i = 0; i < num_blks; i++) {
			asm_data->SRCARG = (char *) s + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			asm_data->DESTARG = (char *) d + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			if (i < blk_containing_n) asm_data->SRC2ARG = (char *) asm_data->COPYZERO;		// This block is before n
			else if (i == blk_containing_n) asm_data->SRC2ARG = (char *) asm_data->COPYZERO + 8;	// This block contains n
			else asm_data->SRC2ARG = (char *) asm_data->COPYZERO + 24;				// This block is past n
			gw_copyzero (gwdata, asm_data);
		}
	}

/* Do the copyzero the old-fashioned way -- in one call */
/* Call assembly language copy-and-zeroing routine */

	else {
		asm_data->SRCARG = s;
		asm_data->DESTARG = d;
		asm_data->NUMARG = n;
		gw_copyzero (gwdata, asm_data);
	}

/* Copy the unnormalized add counter and clear the has been completely/partially FFTed flag. */

	unnorms (d) = unnorms (s);
	FFT_state (d) = NOT_FFTed;
	gwdata->read_count += 1;
	gwdata->write_count += 1;
}

/* Set the constant which the results of a multiplication should be multiplied by. */
/* Use this routine in conjunction with the GWMUL_MULBYCONST multiplication option. */

void gwsetmulbyconst (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	long	val)
{
	struct gwasm_data *asm_data;
	double	ktimesval, big_word;

/* If mul-by-const is already set, return */

	if (gwdata->mulbyconst == val) return;
	gwdata->mulbyconst = (int) val;

/* MMGW reduction requires twice the work */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwsetmulbyconst (gwdata->cyclic_gwdata, val);
		gwsetmulbyconst (gwdata->negacyclic_gwdata, val);
		return;
	}

/* Perform common computations */

	asm_data = (struct gwasm_data *) gwdata->asm_data;
	ktimesval = gwdata->k * (double) val;
	big_word = pow ((double) gwdata->b, gwdata->NUM_B_PER_SMALL_WORD + 1);

/* Adjust the AVX-512 assembly language constants affected by mulbyconst */

	if (gwdata->cpu_flags & CPU_AVX512F) {

/* Save mulbyconst and -c * mulbyconst as a double */

		asm_data->u.zmm.ZMM_MULCONST = (double) val;
		asm_data->u.zmm.ZMM_MINUS_C_TIMES_MULCONST = (double) -gwdata->c * (double) val;

/* Split k*mulconst for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k*mulconst if k*mulconst * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (fabs (ktimesval) * big_word < 562949953421312.0)
			asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE = 0.0;
		else if (ktimesval > 0.0)
			asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE = floor (ktimesval / big_word);
		else
			asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE = -floor (-ktimesval / big_word);
		asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_SMALL_BASE = asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE * (double) gwdata->b;
		asm_data->u.zmm.ZMM_K_TIMES_MULCONST_LO = ktimesval - asm_data->u.zmm.ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE * big_word;
	}

/* Adjust the AVX assembly language constants affected by mulbyconst */

	else if (gwdata->cpu_flags & CPU_AVX) {

/* Save mulbyconst and -c * mulbyconst as a double */

		asm_data->u.ymm.YMM_MULCONST[0] = asm_data->u.ymm.YMM_MULCONST[1] =
		asm_data->u.ymm.YMM_MULCONST[2] = asm_data->u.ymm.YMM_MULCONST[3] = (double) val;
		asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST[0] = asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST[1] =
		asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST[2] = asm_data->u.ymm.YMM_MINUS_C_TIMES_MULCONST[3] = (double) -gwdata->c * (double) val;

/* Split k*mulconst for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k*mulconst if k*mulconst * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (fabs (ktimesval) * big_word < 562949953421312.0)
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[0] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[1] =
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[2] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[3] = 0.0;
		else if (ktimesval > 0.0)
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[0] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[1] =
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[2] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[3] =
				floor (ktimesval / big_word) * big_word;
		else
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[0] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[1] =
			asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[2] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[3] =
				-floor (-ktimesval / big_word) * big_word;
		asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO[0] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO[1] =
		asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO[2] = asm_data->u.ymm.YMM_K_TIMES_MULCONST_LO[3] =
			ktimesval - asm_data->u.ymm.YMM_K_TIMES_MULCONST_HI[0];
	}

/* Adjust the SSE2 assembly language constants affected by mulbyconst */

	else if (gwdata->cpu_flags & CPU_SSE2) {

/* Save mulbyconst and -c * mulbyconst as a double */

		asm_data->u.xmm.XMM_MULCONST[0] = asm_data->u.xmm.XMM_MULCONST[1] = (double) val;
		asm_data->u.xmm.XMM_MINUS_C_TIMES_MULCONST[0] = asm_data->u.xmm.XMM_MINUS_C_TIMES_MULCONST[1] = (double) -gwdata->c * (double) val;

/* Split k*mulconst for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k*mulconst if k*mulconst * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		if (fabs (ktimesval) * big_word < 562949953421312.0)
			asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[0] = asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[1] = 0.0;
		else if (ktimesval > 0.0)
			asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[0] = asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[1] =
				floor (ktimesval / big_word) * big_word;
		else
			asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[0] = asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[1] =
				-floor (-ktimesval / big_word) * big_word;
		asm_data->u.xmm.XMM_K_TIMES_MULCONST_LO[0] =
		asm_data->u.xmm.XMM_K_TIMES_MULCONST_LO[1] = ktimesval - asm_data->u.xmm.XMM_K_TIMES_MULCONST_HI[0];
	}

/* Adjust the x87 assembly language constants affected by mulbyconst */

	else {

/* Save mulbyconst and -c * mulbyconst as a double */

		asm_data->u.x87.MULCONST = (double) val;
		asm_data->u.x87.MINUS_C_TIMES_MULCONST = (double) -gwdata->c * (double) val;

/* Split k*mulconst for zero-padded FFTs emulating modulo k*b^n+c.  Note we only need */
/* to split k*mulconst if k*mulconst * big_word exceeds what a floating point register can hold. */
/* 2^49 should give us enough room to handle a carry and still round properly. */	

		asm_data->u.x87.K_TIMES_MULCONST_HI = floor (ktimesval / big_word) * big_word;
		asm_data->u.x87.K_TIMES_MULCONST_LO = ktimesval - asm_data->u.x87.K_TIMES_MULCONST_HI;
		big_word = big_word * big_word;
		asm_data->u.x87.K_TIMES_MULCONST_HI_2 = floor (asm_data->u.x87.K_TIMES_MULCONST_HI / big_word) * big_word;
		asm_data->u.x87.K_TIMES_MULCONST_HI_1 = asm_data->u.x87.K_TIMES_MULCONST_HI - asm_data->u.x87.K_TIMES_MULCONST_HI_2;
	}
}

/* Add a small constant at the specified power of b after the next multiplication. */
/* That is, value*b^power_of_b is added to the next multiplication result.  This only works if k=1.  Not compatible with postaddin. */

void gwsetaddinatpowerofb (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	long	value,
	unsigned long power_of_b)
{
	unsigned long word, b_in_word;

	ASSERTG (gwdata->k == 1.0 || value == 0);
	ASSERTG (!gwdata->GENERAL_MOD && !gwdata->GENERAL_MMGW_MOD);

/* Handle value of zero */

	if (value == 0) {
		gwdata->asm_addin_value = 0.0;
		return;
	}

/* Tell assembly code to add the shifted value to the multiplication result. */

	bitaddr (gwdata, power_of_b, &word, &b_in_word);
	raw_gwsetaddin (gwdata, word, &gwdata->asm_addin_value, value * pow ((double) gwdata->b, b_in_word));
}

/* Routine that tells the assembly code to add a small value (prior to any mul-by-const) to the results of each multiply. */

void gwsetaddin (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	long	value)
{
	unsigned long word, b_in_word;

	ASSERTG (labs(value) <= 2147483647);		// Limit add-in to 32 bits, though I don't see why 45 or even more bits would not work

/* If this is a case we must emulate, deallocate cached GW_ADDIN, remember addin value */

	if ((gwdata->k != 1.0 && labs (gwdata->c) != 1) || gwdata->GENERAL_MMGW_MOD) {
		if (value == gwdata->emulate_addin_value) return;
		gwdata->emulate_addin_value = value;
		gwfree (gwdata, gwdata->GW_ADDIN);
		gwdata->GW_ADDIN = NULL;
		return;
	}

/* Handle value of zero */

	if (value == 0) {
		gwdata->asm_addin_value = 0.0;
		return;
	}

/* In a zero-padded FFT, the value is added into ZPAD0 */

	if (gwdata->ZERO_PADDED_FFT) {
		// when c = -1, 1 = b^n
		// when c = 1, -1 = b^n, 1 = -b^n
		// when c = -3, 3 = b^n, 1 = b^n - 2
		// when c = 3, -3 = b^n, 3 = -b^n, 1 = -b^n - 2
		gwdata->asm_addin_value = (double) (gwdata->c < 0 ? value : -value);
		return;
	}

/* If k is 1, add the value into the first FFT word */

	if (gwdata->k == 1.0) {
		raw_gwsetaddin (gwdata, 0, &gwdata->asm_addin_value, value);
		return;
	}

/* If value is a multiple of b, "shift" it right and increment b count.  This will ensure that we modify the proper FFT word. */
// DEPRECATED.  Since addin_value and postaddin_value share the same addin_row and addin_offset, we must make sure that either
// both are shifted equally.  Example: AVX-512 6583*4^9534-1 fails QA.  The easiest fix is to shift neither value.
	// for (b_in_word = 0; value && value % (long) gwdata->b == 0; value = value / (long) gwdata->b) b_in_word++;
	b_in_word = 0;

/* Convert the input value to 1/k format.  Case 1 (k*b^n-1): Inverse of k is b^n. */
/* Case 3 (k*b^n+1): Inverse of k is -b^n.  No other cases can be handled. */

	if (gwdata->c == -1) {
		bitaddr (gwdata, gwdata->n + b_in_word, &word, &b_in_word);
	}
	else if (gwdata->c == 1) {
		bitaddr (gwdata, gwdata->n + b_in_word, &word, &b_in_word);
		value = -value;
	}

/* Tell assembly code to add the shifted value to the multiplication result. */

	raw_gwsetaddin (gwdata, word, &gwdata->asm_addin_value, value * pow ((double) gwdata->b, b_in_word));
}

/* Similar to gwsetaddin, this routine adds a very small constant to the result of a multiplication at virtually no cost.  This routine is only useful */
/* if you need to use both GWMUL_ADDINCONST and GWMUL_MULBYCONST.  The addin is done after the mul-by-const operation.  WARNING: The addition is done */
/* after carry propagation has occurred.  Furthermore, for numbers of the k*b^n+/-1 the addin may not be applied to the least significant bits of the */
/* FFT word.  If the adding value exceeds the "b" in k*b^n+c, then you risk dangerous round off errors. */

void gwsetpostmulbyconstaddin (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	int	value)
{
	ASSERTG (gwdata->k == 1.0 || abs(value) <= (int) (2*gwdata->b));	// Sensible(?) limit on add-in value

/* If this is a case we must emulate, deallocate cached GW_ADDIN, remember addin value */

	if ((gwdata->k != 1.0 && labs (gwdata->c) != 1) || gwdata->GENERAL_MMGW_MOD) {
		if (value == gwdata->emulate_postaddin_value) return;
		gwdata->emulate_postaddin_value = value;
		gwfree (gwdata, gwdata->GW_POSTADDIN);
		gwdata->GW_POSTADDIN = NULL;
		return;
	}

/* Handle value of zero */

	if (value == 0) {
		gwdata->asm_postaddin_value = 0.0;
		return;
	}

/* In a zero-padded FFT, the value is added into ZPAD0 */

	if (gwdata->ZERO_PADDED_FFT) {
		// when c = -1, 1 = b^n
		// when c = 1, -1 = b^n, 1 = -b^n
		// when c = -3, 3 = b^n, 1 = b^n - 2
		// when c = 3, -3 = b^n, 3 = -b^n, 1 = -b^n - 2
		gwdata->asm_postaddin_value = (double) (gwdata->c < 0 ? value : -value);
		return;
	}

/* If k is 1, add the value into the first FFT word */

	if (gwdata->k == 1.0) {
		raw_gwsetaddin (gwdata, 0, &gwdata->asm_postaddin_value, value);
	}

/* If value is a multiple of b, "shift" it right and increment b count.  This will ensure that we modify the proper FFT word. */

	else {
		unsigned long word, b_in_word = 0;
		// DEPRECATED.  Since addin_value and postaddin_value share the same addin_row and addin_offset, we must make sure that either
		// both are shifted equally.  Example: AVX-512 6583*4^9534-1 fails QA.  The easiest fix is to shift neither value.
		// for (b_in_word = 0; value && value % (long) gwdata->b == 0; value = value / (long) gwdata->b)
		//	b_in_word++;

/* Convert the input value to 1/k format.  Case 1 (k*b^n-1): Inverse of k is b^n. */
/* Case 3 (k*b^n+1): Inverse of k is -b^n.  No other cases can be handled. */

		if (gwdata->c == -1) {
			bitaddr (gwdata, gwdata->n + b_in_word, &word, &b_in_word);
		}
		else if (gwdata->c == 1) {
			bitaddr (gwdata, gwdata->n + b_in_word, &word, &b_in_word);
			value = -value;
		}

/* Tell assembly code to add the shifted value to the multiplication result. */

		raw_gwsetaddin (gwdata, word, &gwdata->asm_postaddin_value, value * pow ((double) gwdata->b, b_in_word));
	}
}

/* Routine that tells the assembly code to add a small value to the results of each multiply */

void raw_gwsetaddin (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long word,
	double	*ptr,	     
	double	val)
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned long row;

/* Protect against cloning until ADDIN_ROW, ADDIN_OFFSET, and asm_addin_value have all been changed. */
/* One could argue that protecting against this inconsistency is useless.  If you clone while the parent is in the middle of this routine, then the */
/* caller has a race condition where he has no idea whether he's cloned the old or new addin value. */

	if (gwdata->clone_of == NULL) gwmutex_lock (&gwclone_lock);

/* Handle calculations for AVX-512 FFTs.  All two-pass FFTs use a scratch area. */

	if (gwdata->cpu_flags & CPU_AVX512F) {

/* For one pass FFTs, use the offset to the FFT data value */

		if (gwdata->PASS1_SIZE == 0) {
			asm_data->ADDIN_ROW = 0;
			asm_data->ADDIN_OFFSET = addr_offset (gwdata, word);
		}

/* For two-pass FFTs, compute the first FFT value in the pass 1 block (ADDIN_ROW) */
/* and the offset when the target data is in the scratch area. */

		else {
			row = word % gwdata->PASS2_SIZE;
			asm_data->ADDIN_ROW = row & ~(gwdata->PASS1_CACHE_LINES - 1);
			asm_data->ADDIN_OFFSET = (row - asm_data->ADDIN_ROW) * 128;
			if (word >= gwdata->FFTLEN / 2) asm_data->ADDIN_OFFSET += 64;
			asm_data->ADDIN_OFFSET += ((word / gwdata->PASS2_SIZE) & 7) * 8;
			row = (word % (gwdata->FFTLEN / 2)) / (8 * gwdata->PASS2_SIZE);
			asm_data->ADDIN_OFFSET += row * (gwdata->PASS1_CACHE_LINES * 128 + (uint32_t) asm_data->normblkdst);
			asm_data->ADDIN_OFFSET += (row >> 3) * (uint32_t) asm_data->normblkdst8;
		}
	}

/* Handle calculations for AVX FFTs.  All two-pass FFTs use a scratch area. */

	else if (gwdata->cpu_flags & CPU_AVX) {

/* For two-pass FFTs, compute the first FFT value in the pass 1 block (ADDIN_ROW) */
/* and the offset when the target data is in the scratch area. */

		if (gwdata->PASS2_SIZE) {
			row = word % gwdata->PASS2_SIZE;
			asm_data->ADDIN_ROW = row & ~(gwdata->PASS1_CACHE_LINES - 1);
			asm_data->ADDIN_OFFSET = (row - asm_data->ADDIN_ROW) * 64;
			if (word >= gwdata->FFTLEN / 2) asm_data->ADDIN_OFFSET += 32;
			asm_data->ADDIN_OFFSET += ((word / gwdata->PASS2_SIZE) & 3) * 8;
			row = (word % (gwdata->FFTLEN / 2)) / (4 * gwdata->PASS2_SIZE);
			asm_data->ADDIN_OFFSET += row * (gwdata->PASS1_CACHE_LINES * 64 + (uint32_t) asm_data->normblkdst);
			asm_data->ADDIN_OFFSET += (row >> 3) * (uint32_t) asm_data->normblkdst8;
		}

/* For one pass FFTs, use the offset to the FFT data value */

		else
			asm_data->ADDIN_OFFSET = addr_offset (gwdata, word);

	}

/* Handle calculations for SSE2 FFTs */

	else if (gwdata->cpu_flags & CPU_SSE2) {

/* Compute the offset to the FFT data value */

		asm_data->ADDIN_OFFSET = addr_offset (gwdata, word);

/* If this is a one-pass SSE2 FFT, then we need to tell the assembly code */
/* the affected "row", that is which set of data the add-in will take place. */

		if (gwdata->PASS2_SIZE == 0) {
			row = asm_data->ADDIN_OFFSET & 31;
			if (row == 8) asm_data->ADDIN_OFFSET += 8;
			if (row == 16) asm_data->ADDIN_OFFSET -= 8;
		}

/* Factor in the blkdst value in xfft3.mac to compute the two pass SSE2 addin_offset. */

		else {
			unsigned long num_rows;

			num_rows = gwdata->PASS2_SIZE >> 1;
			row = word % num_rows;
			asm_data->ADDIN_ROW = row & ~(gwdata->PASS1_CACHE_LINES - 1);
			asm_data->ADDIN_OFFSET -= (row >> 7) * 128 + (row / gwdata->PASS1_CACHE_LINES) * gwdata->PASS1_CACHE_LINES * 64;

/* This case is particularly nasty as we have to convert the FFT data offset */
/* into a scratch area offset.  In assembly language terms, this means */
/* subtracting out multiples of blkdst and adding in multiples of clmblkdst */
/* and clmblkdst8. */

			if (gwdata->SCRATCH_SIZE) {
				unsigned long blkdst;

				blkdst = addr_offset (gwdata, gwdata->PASS2_SIZE);
				row = asm_data->ADDIN_OFFSET / blkdst;
				asm_data->ADDIN_OFFSET -= row * blkdst;
				asm_data->ADDIN_OFFSET += row * (gwdata->PASS1_CACHE_LINES * 64);
				asm_data->ADDIN_OFFSET += (row >> 3) * (uint32_t) asm_data->normblkdst8;
			}
		}
	}

/* Handle calculations for x87 FFTs */

	else {

/* Compute the offset to the FFT data value */

		asm_data->ADDIN_OFFSET = addr_offset (gwdata, word);

/* x87 FFTs use a scratch area.  Like the SSE2 code */
/* we have to convert the FFT data offsets for two-pass FFTs. */

		if (gwdata->PASS2_SIZE) {
			unsigned long num_cache_lines, cache_line;

			num_cache_lines = gwdata->PASS2_SIZE >> 1;
			cache_line = ((word >> 1) & (num_cache_lines - 1));

			asm_data->ADDIN_ROW = ((num_cache_lines>>7) - (cache_line>>7)) * 65536 +
					(128 / gwdata->PASS1_CACHE_LINES -
					(cache_line & 127) / gwdata->PASS1_CACHE_LINES) * 256;
			asm_data->ADDIN_OFFSET -= (cache_line >> 7) * 64 +
					(cache_line / gwdata->PASS1_CACHE_LINES) *
					gwdata->PASS1_CACHE_LINES * 32;

/* This case is particularly nasty as we have to convert the FFT data offset */
/* into a scratch area offset.  In assembly language terms, this means */
/* subtracting out multiples of blkdst and adding in multiples of clmblkdst */
/* and clmblkdst32. */

			if (gwdata->SCRATCH_SIZE) {
				unsigned long blkdst;

				blkdst = addr_offset (gwdata, gwdata->PASS2_SIZE);
				row = asm_data->ADDIN_OFFSET / blkdst;
				asm_data->ADDIN_OFFSET -= row * blkdst;
				asm_data->ADDIN_OFFSET += row * (gwdata->PASS1_CACHE_LINES * 32);

/* Handle the FFTs where clmblkdst32 is used */

				if (((gwdata->FFTLEN / gwdata->PASS2_SIZE) >> 1) >= 128)
					asm_data->ADDIN_OFFSET += (row >> 5) * 64;
			}
		}
	}

/* Caller may only be interested in ADDIN_OFFSET */

	if (ptr != NULL) {

/* Handle AVX-512 FFTs.  They are normalized differently than FMA3 and earlier FFTs */

	    if (gwdata->cpu_flags & CPU_AVX512F) {

/* Post addins do not need a weight.  Rational FFTs are not normalized, don't need to apply a weight to the addin value. */
/* Exceptions are two-pass negacyclic FFTs which have a sine component as part of the inverse group multiplier. */

		if (ptr == &gwdata->asm_postaddin_value || (gwdata->RATIONAL_FFT && !(gwdata->PASS1_SIZE && gwdata->NEGACYCLIC_FFT))) {
			*ptr = val;
		}

/* AVX-512 FFT's ADDIN_VALUE is partially normalized (since we moved the weighting from the last_unfft macro to the normalize */
/* code to take advantage of an FMA opportunity.  Apply a partial weight to the addin value.  Also, if it's a negacyclic */
/* FFT then we need to adjust for the roots-of-minus-one sine optimization too. */

		else {
			unsigned long upper_avx512_word, grp;
			double	ttmp;
			// Mimic code from building inverse multipliers in zr4dwpn_build_norm_table
			upper_avx512_word = (gwdata->PASS1_SIZE == 0) ? 8 : gwdata->PASS2_SIZE;
			grp = word - word % upper_avx512_word;
			if (gwdata->PASS1_SIZE && gwdata->NEGACYCLIC_FFT) {
				unsigned long sine_word;
				sine_word = grp % (gwdata->FFTLEN / 2);
				gwfft_weights_times_sine (gwdata->dd_data, grp, sine_word, gwdata->FFTLEN * 2, NULL, &ttmp);
			} else
				ttmp = gwfft_weight_inverse (gwdata->dd_data, grp);
			*ptr = val / ttmp;
		}
	    }

/* Handle r4dwpn FFTs which are only partially normalized */

	    else if (gwdata->FFT_TYPE == FFT_TYPE_RADIX_4_DWPN) {
		*ptr = val * gwfft_partial_weight_sloppy (gwdata->dd_data, word, dwpn_col (gwdata, word));
	    }

/* Set the addin value - multiply it by two-to-phi and FFTLEN/2/k. */

	    else {
		*ptr = val * gwfft_weight_sloppy (gwdata->dd_data, word);
		if (ptr == &gwdata->asm_addin_value) *ptr *= gwdata->FFTLEN * 0.5 / gwdata->k;
	    }
	}

/* Release clone lock now that ADDIN_ROW, ADDIN_OFFSET, and asm_addin_value have all been changed */

	if (gwdata->clone_of == NULL) gwmutex_unlock (&gwclone_lock);
}

/* Initialize the cached GW_ADDIN value */

void init_GW_ADDIN (
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	if (gwdata->GW_ADDIN == NULL && gwdata->emulate_addin_value) {
		gwdata->GW_ADDIN = gwalloc_internal (gwdata);
		dbltogw (gwdata, (double) gwdata->emulate_addin_value, gwdata->GW_ADDIN);
	}
	if (gwdata->GW_POSTADDIN == NULL && gwdata->emulate_postaddin_value) {
		gwdata->GW_POSTADDIN = gwalloc_internal (gwdata);
		dbltogw (gwdata, (double) gwdata->emulate_postaddin_value, gwdata->GW_POSTADDIN);
	}
}

/* Montgomery-McLaughlin-Gallot-Woltman reduction.  Multiplies T_R by 2/R where R=2^n-1. */
void MMGW_redc (gwhandle *gwdata, gwnum T_R, gwnum T_Q)
{
	gwmul3 (gwdata->cyclic_gwdata, T_R, gwdata->Np_R, T_R, 0);			// T_R = T_R * Np_R mod R
	*T_R += 2.0;									// T_R += 2
	gwmul3 (gwdata->negacyclic_gwdata, T_R, gwdata->N_Q, T_R, 0);			// T_R = T_R * N_Q mod Q
	gwsub3o (gwdata->negacyclic_gwdata, T_R, T_Q, T_R, GWADD_FORCE_NORMALIZE);	// T_R = T_R - T_Q
}

/* Special version of Montgomery-McLaughlin-Gallot-Woltman reduction that uses gwmulsub4 to save one transform */
void MMGW_redc4 (gwhandle *gwdata, gwnum T_R, gwnum T_Q)
{
	gwmul3 (gwdata->cyclic_gwdata, T_R, gwdata->Np_R, T_R, 0);			// T_R = T_R * Np_R mod R
	*T_R += 2.0;									// T_R += 2
	gwmulsub4 (gwdata->negacyclic_gwdata, T_R, gwdata->N_Q, T_Q, T_R, 0);		// T_R = T_R * N_Q - T_Q mod Q
}

/* Special version of Montgomery-McLaughlin-Gallot-Woltman reduction that uses gwmulmulsub5 to save one transform */
void MMGW_redc5 (gwhandle *gwdata, gwnum T_R, gwnum s1_Q, gwnum s2_Q)
{
	gwmul3 (gwdata->cyclic_gwdata, T_R, gwdata->Np_R, T_R, 0);			// T_R = T_R * Np_R mod R
	*T_R += 2.0;									// T_R += 2
	gwmulmulsub5 (gwdata->negacyclic_gwdata, T_R, gwdata->N_Q, s1_Q, s2_Q, T_R, 0);	// T_R = T_R * N_Q - s1_Q * s2_Q mod Q
}

/* Special version for gwtogiant of Montgomery-McLaughlin-Gallot-Woltman reduction that takes just one argument */
void MMGW_redc1 (gwhandle *gwdata, gwnum T, gwnum T_R)
{
	ASSERTG (T != T_R);
	gwmul3 (gwdata->cyclic_gwdata, T, gwdata->Np_R, T_R, GWMUL_PRESERVE_S1);	// T_R = T * Np_R mod R
	*T_R += 2.0;									// T_R += 2
	if (FFT_state (T) != FULLY_FFTed) {
		gwmul3 (gwdata->negacyclic_gwdata, T_R, gwdata->N_Q, T_R, 0);		// T_R = T_R * N_Q mod Q
		gwsub3quick (gwdata->negacyclic_gwdata, T_R, T, T_R);			// T_R = T_R - T
	} else {
		gwnum T_Q = negacyclic_gwnum (gwdata, T);
		ASSERTG (muladd_safe (gwdata->negacyclic_gwdata, 0, 0, unnorms (T_Q)));
		gwmulsub4 (gwdata->negacyclic_gwdata, T_R, gwdata->N_Q, T_Q, T_R, 0);	// T_R = T_R * N_Q - T_Q mod Q
	}
}

/********************************************************/
/* Routines to convert between gwnums and other formats */
/********************************************************/

/* Convert a double to a gwnum */

void dbltogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	d,		/* Input number */
	gwnum	g)		/* Gwnum value to set */
{
	stackgiant(tmp, 2);

	dbltog (d, tmp);
	gianttogw (gwdata, tmp, g);
}

/* Convert a int64_t to a gwnum */

void s64togw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	int64_t d,		/* Input number */
	gwnum	g)		/* Gwnum value to set */
{
	stackgiant(tmp, 2);

	slltog (d, tmp);
	gianttogw (gwdata, tmp, g);
}

/* Convert a uint64_t to a gwnum */

void u64togw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	uint64_t d,		/* Input number */
	gwnum	g)		/* Gwnum value to set */
{
	stackgiant(tmp, 2);

	ulltog (d, tmp);
	gianttogw (gwdata, tmp, g);
}

/* Convert a binary value to a gwnum */

void binarytogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const uint32_t *array,	/* Array containing the binary value */
	uint32_t arraylen,	/* Length of the array */
	gwnum	n)		/* Destination gwnum */
{
	giantstruct tmp;
	tmp.sign = arraylen;
	tmp.n = (uint32_t *) array;
	while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
	gianttogw (gwdata, &tmp, n);
}

/* Convert a binary value to a gwnum */

void binary64togw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const uint64_t *array,	/* Array containing the binary value */
	uint64_t arraylen,	/* Length of the array */
	gwnum	n)		/* Destination gwnum */
{
	giantstruct tmp;
	tmp.sign = (int) arraylen * sizeof (uint64_t) / sizeof (uint32_t);
	tmp.n = (uint32_t *) array;
	while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
	gianttogw (gwdata, &tmp, n);
}

/* Convert a binary value to a gwnum */

void binarylongstogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const unsigned long *array, /* Array containing the binary value */
	unsigned long arraylen,	/* Length of the array */
	gwnum	n)		/* Destination gwnum */
{
	giantstruct tmp;
	tmp.sign = arraylen * sizeof (unsigned long) / sizeof (uint32_t);
	tmp.n = (uint32_t *) array;
	while (tmp.sign && tmp.n[tmp.sign-1] == 0) tmp.sign--;
	gianttogw (gwdata, &tmp, n);
}

/* Convert a giant to gwnum FFT format */

void gianttogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	giant	a,
	gwnum	g)
{

/* For general mod, convert to Montgomery-McLaughlin-Gallot-Woltman format (traditional Montgomery format divided by 2). */
/* Multiply value by R^2/4.  Then apply MMGW_redc which multiplies by 2/R.  This gives us the MMGW format: a * R/2 mod N. */
/* NOTE:  There is one exception -- when we are initializing R2_4 don't do this! */

	if (gwdata->GENERAL_MMGW_MOD) {
		gianttogw (gwdata->negacyclic_gwdata, a, g);
		if (g != gwdata->R2_4) {
			if (labs (a->sign) > 4) gwmul3_carefully (gwdata, g, gwdata->R2_4, g, 0); // General mod numbers are more likely to need careful muls
			else gwmul3 (gwdata, g, gwdata->R2_4, g, 0);				  // Don't careful mul small numbers (avoids infinite recursion)
		}
		return;
	}

/* Jean Penne requested that we optimize the small number cases.  Setting the gwnum to zero is real easy. */

	if (a->sign == 0) {
		memset (g, 0, gwnum_datasize (gwdata));
	}

/* Small numbers can also be optimized for many moduli by zeroing all the */
/* FFT data using memset and then setting only the affected FFT elements. */

	else if (labs (a->sign) <= 2 && (gwdata->k == 1.0 || labs (gwdata->c) == 1)) {
		uint64_t value_to_convert, low_addin;
		int	i;

/* Zero the FFT data, init the small value to convert to a gwnum */

		memset (g, 0, gwnum_datasize (gwdata));
		value_to_convert = a->n[0];
		if (labs (a->sign) == 2) value_to_convert += ((uint64_t) a->n[1]) << 32;

/* To make the mod k*b^n+c step faster, gwnum's are pre-multiplied by 1/k. */
/* Case 1 (k*b^n-1): Inverse of k is b^n.  Case 2 (k*b^n+1): Inverse of k is -b^n. */
/* Since the value we are adding (or subtracting) from the top FFT words can be */
/* greater than k, we must divide value by k and wrap-around to add it in to the */
/* lowest FFT words.  The remainder gets added to (or subtracted from) the upper FFT words. */

		uint64_t small_maxval = intpow (gwdata->b, gwdata->NUM_B_PER_SMALL_WORD);
		if (gwdata->k > 1.0) {
			uint64_t quotient, remainder, high_addin;
			unsigned long word, bit_in_word;

			quotient = value_to_convert / (uint64_t) gwdata->k;
			remainder = value_to_convert - quotient * (uint64_t) gwdata->k;
			if (gwdata->c == -1) {
				low_addin = quotient;
				high_addin = remainder;
			} else {
				low_addin = quotient + 1;
				high_addin = (uint64_t) gwdata->k - remainder;
			}

/* Add the high_addin value across the top FFT words */

			bitaddr (gwdata, gwdata->n, &word, &bit_in_word);
			for (i = word; high_addin; i++, bit_in_word = 0) {
				long	value;

				uint64_t maxval = small_maxval;
				if (is_big_word (gwdata, i)) maxval *= gwdata->b;

				if (bit_in_word == 0) {
					quotient = high_addin / maxval;
					remainder = high_addin - quotient * maxval;
					value = (long) remainder;
					high_addin = quotient;
				} else {
					uint64_t pow_fudge, fudged_maxval;
					pow_fudge = intpow (gwdata->b, bit_in_word);
					fudged_maxval = maxval / pow_fudge;
					quotient = high_addin / fudged_maxval;
					remainder = high_addin - quotient * fudged_maxval;
					value = (long) (remainder * pow_fudge);
					high_addin = quotient;
				}

				if ((uint64_t) value > (maxval >> 1)) {
					value = value - (long) maxval;
					high_addin += 1;
				}
				if (i == gwdata->FFTLEN - 1) {
					value = value + (long) high_addin * (long) maxval;
					high_addin = 0;
				}
				set_fft_value (gwdata, g, i, a->sign > 0 ? value : -value);
			}
		}

/* If k is 1, simply copy the giant value to the low FFT words */

		else
			low_addin = value_to_convert;

/* Spread the low_addin value across the lowest FFT words as necessary */

		for (i = 0; low_addin; i++) {
			uint64_t maxval = small_maxval;
			if (is_big_word (gwdata, i)) maxval *= gwdata->b;

			long value = (long) (low_addin % maxval);
			low_addin = low_addin / maxval;
			if (value > (long) (maxval >> 1)) {
				value = value - (long) maxval;
				low_addin++;
			}
			set_fft_value (gwdata, g, i, a->sign > 0 ? value : -value);
		}
	}

/* To make the mod k*b^n+c step faster, gwnum's are pre-multiplied by 1/k */
/* If k is greater than 1, then we calculate the inverse of k, multiply */
/* the giant by the inverse of k, and do a mod k*b^n+c. */

	else {
		giant	newg = NULL;

		if (gwdata->k > 1.0) {
			newg = popg (&gwdata->gdata, (((unsigned long) gwdata->bit_length >> 5) + 2) * 2);

			/* Easy case 1 (k*b^n-1): Inverse of k is b^n */

			if (gwdata->c == -1) {
				if (gwdata->b == 2) {
					gtog (a, newg);
					gshiftleft (gwdata->n, newg);
				} else {
					itog (gwdata->b, newg);
					power (newg, gwdata->n);
					mulgi (&gwdata->gdata, a, newg);
				}
			}

			/* Easy case 2 (k*b^n+1): Inverse of k is -b^n */

			else if (gwdata->c == 1) {
				if (gwdata->b == 2) {
					gtog (a, newg);
					gshiftleft (gwdata->n, newg);
					negg (newg);
				} else {
					itog (gwdata->b, newg);
					power (newg, gwdata->n);
					negg (newg);
					mulgi (&gwdata->gdata, a, newg);
				}
			}

			else {				/* General inverse case */
				giant	n;
				n = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 2);
				ultog (gwdata->b, n);		/* Compute k*b^n+c */
				power (n, gwdata->n);
				dblmulg (gwdata->k, n);
				iaddg (gwdata->c, n);
				dbltog (gwdata->k, newg);	/* Compute 1/k */
				invg (n, newg);
				ASSERTG (newg->sign > 0);	/* Assert inverse found */
				mulgi (&gwdata->gdata, a, newg); /* Multiply input num by 1/k */
				pushg (&gwdata->gdata, 1);
			}

			specialmodg (gwdata, newg);
			a = newg;
		}

/* Now convert the giant to gwnum format.  For base 2 we simply copy bits. */

		if (gwdata->b == 2) {
			unsigned long limit;
			int	bits, bits1, bits2, e1len, accumbits;
			int64_t	accum, value;
			uint32_t *e1;

			// Figure out how many FFT words we will need to set
			limit = (unsigned long) ceil ((double) bitlen (a) / (gwdata->avg_num_b_per_word * log2 (gwdata->b)));
			if (limit > gwdata->FFTLEN) limit = gwdata->FFTLEN;
			if (gwdata->ZERO_PADDED_FFT && limit > gwdata->FFTLEN / 2 + 4) limit = gwdata->FFTLEN / 2 + 4;

			e1len = abs (a->sign);
			e1 = a->n;

			bits1 = gwdata->NUM_B_PER_SMALL_WORD;
			bits2 = bits1 + 1;

			accum = 0;
			accumbits = 0;

			gwiter iter;
			for (gwiter_init_write_only (gwdata, &iter, g); gwiter_index (&iter) < limit; gwiter_next (&iter)) {

				// If needed, add another 32 input bits to accumulator
				if (accumbits < 28) {
					if (e1len > 0) {
						if (a->sign > 0) accum += ((int64_t) *e1) << accumbits;
						else accum -= ((int64_t) *e1) << accumbits;
						e1++, e1len--;
					}
					accumbits += 32;
				}

				// Process top word without sign extension, otherwise grab bits with sign extension.
				// Special case zero bits as shift left 64 may be undefined.
				bits = (gwiter_index (&iter) == limit - 1) ? 32 : gwiter_is_big_word (&iter) ? bits2 : bits1;
				value = bits ? (accum << (64 - bits)) >> (64 - bits) : 0;
				gwiter_set_fft_value (&iter, (int32_t) value);
				accum = (accum - value) >> bits;
				accumbits -= bits;
			}

			// Clear the upper words
			for ( ; gwiter_index (&iter) < gwdata->FFTLEN; gwiter_next (&iter)) gwiter_set_fft_value (&iter, 0);
		}

/* Otherwise (non-base 2), we do a recursive divide and conquer radix conversion. */

		else {
			if (a->sign < 0) {
				newg = popg (&gwdata->gdata, (((unsigned long) gwdata->bit_length >> 5) + 2) * 2);
				gtog (a, newg);
				specialmodg (gwdata, newg);
				a = newg;
			}
			nonbase2_gianttogw (gwdata, a, g);
		}

/* Free allocated memory */

		if (a == newg) pushg (&gwdata->gdata, 1);
	}

/* Clear various flags, update counts */

	unnorms (g) = 0.0f;		/* Set unnormalized add counter */
	FFT_state (g) = NOT_FFTed;	/* Clear has been completely/partially FFTed flag */
	gwdata->write_count += 1;
}

/* Convert a gwnum to a binary value.  Returns the number of 32-bit values written to the array.  The array is NOT zero-padded.  Returns a */
/* negative number if an error occurs during the conversion.  An error can happen if the FFT data contains a NaN or infinity value. */

long gwtobinary (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	uint32_t *array,	/* Array to contain the binary value */
	uint32_t arraylen)	/* Maximum size of the array */
{
	int	err_code;

	ASSERTG (arraylen * 32 > gwdata->bit_length);

/* Make sure data is not FFTed.  Caller should really try to avoid this scenario. */

	if (FFT_state (n) != NOT_FFTed) gwunfft (gwdata, n, n);

/* Convert the number in-place if we can.  gwtogiant has two sources of possibly needing a bigger buffer.  One is the mul-by-k, the other is unnormalized adds. */
/* It is also true that gwnum multiplications do not guarantee perfectly normalized gwnums, so the top FFT word could have an extra bit. */
/* We want to be extra safe, so instead on at least a full extra word. */

	if (arraylen * 32 >= gwdata->bit_length + log2 (gwdata->k) + unnorms (n) + 32.0) {
		giantstruct tmp;
		tmp.n = (uint32_t *) array;
		setmaxsize (&tmp, arraylen);
		err_code = gwtogiant (gwdata, n, &tmp);
		if (err_code < 0) return (err_code);
		gwdata->read_count += 1;
		return (tmp.sign);
	}

/* Convert the number using a bigger buffer */

	else {
		giant tmp = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 5);
		err_code = gwtogiant (gwdata, n, tmp);
		if (err_code < 0) { pushg (&gwdata->gdata, 1); return (err_code); }
		if ((uint32_t) tmp->sign > arraylen) tmp->sign = arraylen;			// Truncate result if necessary
		memcpy (array, tmp->n, tmp->sign * sizeof (uint32_t));
		pushg (&gwdata->gdata, 1);
		gwdata->read_count += 1;
		return (tmp->sign);
	}
}

/* Convert a gwnum to a binary value.  Returns the number of 64-bit values written to the array.  The array is NOT zero-padded.  Returns a */
/* negative number if an error occurs during the conversion.  An error can happen if the FFT data contains a NaN or infinity value. */

long gwtobinary64 (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	uint64_t *array,	/* Array to contain the binary value */
	uint32_t arraylen)	/* Maximum size of the array */
{

// Intel's Endian-ness allows us to use 32-bit gwtobinary with a little extra work if gwtobinary writes an odd number of 32-bit values

	long count = gwtobinary (gwdata, n, (uint32_t *) array, arraylen * 2);
	if (count > 0) {
		bool count_is_odd = (count & 1);			// Detect an odd count of elements written
		count = (count + 1) / 2;				// Halve the count of elements written
		if (count_is_odd) array[count-1] &= 0xFFFFFFFFULL;	// Clear top half of top element
	}
	return (count);
}

/* Convert a gwnum to a binary value.  Returns the number of longs written to the array.  The array is NOT zero-padded.  Returns a */
/* negative number if an error occurs during the conversion.  An error can happen if the FFT data contains a NaN or infinity value. */

long gwtobinarylongs (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	unsigned long *array,	/* Array to contain the binary value */
	unsigned long arraylen)	/* Maximum size of the array */
{
	if (sizeof (unsigned long) == sizeof (uint32_t)) return gwtobinary (gwdata, n, (uint32_t *) array, arraylen);
	else return gwtobinary64 (gwdata, n, (uint64_t *) array, arraylen);
}

/* Convert a gwnum to a giant.  WARNING: Caller must allocate an array that is several words larger than the maximum result that can be returned. */
/* This is a gross kludge that lets gwtogiant use the giant for intermediate calculations. */

int gwtogiant (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	gg,
	giant	v)
{
	stackgiant(k,2);
	int	err_code;
	unsigned long limit;

/* For general mod, convert out of Montgomery-McLaughlin-Gallot-Woltman format (traditional Montgomery format divided by 2) using MMGW_redc. */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwnum newgg = gwalloc (gwdata);
		MMGW_redc1 (gwdata, gg, newgg);
		err_code = gwtogiant (gwdata->negacyclic_gwdata, newgg, v);
		gwfree (gwdata, newgg);
		specialmodg (gwdata, v);
		ASSERTG (v->sign >= 0);
		return (err_code);
	}

/* Make sure data is not FFTed.  Caller should really try to avoid this scenario. */

	if (FFT_state (gg) != NOT_FFTed) gwunfft (gwdata, gg, gg);

/* Set result to zero in case of error.  If caller does not check the returned error code a result value of zero is less likely to cause problems/crashes. */

	v->sign = 0;

/* Rather than a separate never-will-be-multithreaded giants call to mul-by-k, prepare to mul-by-k as we go (at least in the b=2 case). */

	dbltog (gwdata->k, k);
	bool k_is_small = (k->sign <= 1);
	bool k_is_one = (k_is_small && k->n[0] == 1);
#define klo	(k->n[0])
#define khi	(k->n[1])
#define k64	(*(uint64_t *)k->n)
	if (k_is_small) khi = 0;	// Needed for k64 to work

/* If this is a general-purpose mod, then only convert the needed words */
/* which will be less than half the FFT length.  If this is a zero padded */
/* FFT, then only convert a little more than half of the FFT data words. */
/* For a DWT, convert all the FFT data. */

	if (gwdata->GENERAL_MOD) limit = gwdata->GW_GEN_MOD_MAX + 3;
	else if (gwdata->ZERO_PADDED_FFT) limit = gwdata->FFTLEN / 2 + 4;
	else limit = gwdata->FFTLEN;

/* GENERAL_MOD has some strange cases we must handle.  In particular the */
/* last fft word translated can be 2^bits and the next word could be -1, */
/* this must be translated into zero, zero. */

	if (gwdata->GENERAL_MOD) {
		long	val, prev_val;
		while (limit < gwdata->FFTLEN) {
			err_code = get_fft_value (gwdata, gg, limit, &val);
			if (err_code) return (err_code);
			if (val == -1 || val == 0) break;
			limit++;
			ASSERTG (limit <= gwdata->FFTLEN / 2 + 2);
			if (limit > gwdata->FFTLEN / 2 + 2) return (GWERROR_INTERNAL + 9);
		}
		while (limit > 1) {		/* Find top word */
			err_code = get_fft_value (gwdata, gg, limit-1, &prev_val);
			if (err_code) return (err_code);
			if (val != prev_val || val < -1 || val > 0) break;
			limit--;
		}
		limit++;
	}

/* If base is 2 we can simply copy the bits out of each FFT word */

	if (gwdata->b == 2) {
		gwiter	iter;
		int32_t	val;
		int64_t accum;
		int	bits, accumbits;
		uint32_t *outptr, outval;
		uint64_t outcarry;
		uint32_t will_output, cached_zeros;

/* Collect bits until we have all of them */

		accum = 0;
		accumbits = 0;
		outptr = v->n;
		outcarry = 0;
		cached_zeros = 0;
		for (gwiter_init_zero (gwdata, &iter, gg); ; ) {
			// Process next FFT word
			if (gwiter_index (&iter) < limit) {
				err_code = gwiter_get_fft_value (&iter, &val);
				if (err_code) return (err_code);
				bits = gwdata->NUM_B_PER_SMALL_WORD;
				if (gwiter_is_big_word (&iter)) bits++;
				accum += ((int64_t) val) << accumbits;
				accumbits += bits;
				gwiter_next (&iter);
				// See if we have 32-bits to output
				if (accumbits < 32) continue;
			}

			// Extract the 32 output bits
			outval = (uint32_t) accum;
			accum >>= 32;
			accumbits -= 32;

			// Now mul by k
			if (k_is_one)
				will_output = outval;
			else if (k_is_small) {
				uint64_t tmp = (uint64_t) outval * (uint64_t) klo + outcarry;
				will_output = (uint32_t) tmp;
				outcarry = tmp >> 32;
			} else {
				uint64_t tmp = (uint64_t) outval * (uint64_t) klo;
				uint64_t next_outcarry = tmp >> 32;
				tmp = (tmp & 0xFFFFFFFFULL) + outcarry;
				will_output = (uint32_t) tmp;
				outcarry = next_outcarry + (tmp >> 32) + (uint64_t) outval * (uint64_t) khi;
			}
			if (will_output) {
				while (cached_zeros) { *outptr++ = 0; cached_zeros--; }
				*outptr++ = will_output;
			} else {
				cached_zeros++;
			}

			// Are we done?  We need to process limit FFT words plus the outcarry.
			if (gwiter_index (&iter) >= limit && ((accum == 0 && outcarry == 0) || (accum == -1 && outcarry == k64 - 1))) break;
		}

/* Set the length */

		ASSERTG (v->maxsize >= (int) (outptr - v->n));
		v->sign = (int) (outptr - v->n);
		ASSERTG (v->sign == 0 || v->n[v->sign-1] != 0);		// Trailing zeros were eliminated above

/* Divide the upper bits by k, leave the remainder in the upper bits and multiply the quotient by c and subtract that from the lower bits. */

		stackgiant(upper,5);					// Upper bits shouldn't be much more than k^2 (100 bits)
		gtogshiftrightsplit (gwdata->n, v, upper, accum);	// Split v at bit n into two giants optionally negating upper bits
		if (!isZero (upper)) {
			if (k_is_one) {
				imulg (gwdata->c, upper);	// Upper bits times c
				subg (upper, v);		// Lower bits minus upper bits * c
			} else {
				stackgiant(quot,5);
				gtog (upper, quot);
				divg (k, quot);			// Upper bits over k
				stackgiant(tmp,5);
				gtog (k, tmp);
				mulg (quot, tmp);
				subg (tmp, upper);		// Upper bits mod k
				if (upper->sign < 0) iaddg (-1, quot), addg (k, upper); // Make negative remainder positive
				gtogshiftleftunsplit (gwdata->n, upper, v);	// Copy remainder to high bits of v
				imulg (gwdata->c, quot);	// Upper bits over k times c
				subg (quot, v);
			}
		}
	}

/* Otherwise (base is not 2) we must do a radix conversion */

	else {
		err_code = nonbase2_gwtogiant (gwdata, gg, v);
		if (err_code) return (err_code);

/* Since all gwnums are premultiplied by the inverse of k, we must now multiply by k to get the true result. */

		if (!k_is_one) mulgi (&gwdata->gdata, k, v);
	}

#undef klo
#undef khi
#undef k64

/* Make sure we were passed a large enough buffer */

	ASSERTG (v->maxsize >= labs (v->sign));

/* The gwnum is not guaranteed to be smaller than k*b^n+c.  Handle this possibility.  This also converts negative values to positive. */

	specialmodg (gwdata, v);
	ASSERTG (v->sign >= 0);

/* Return success */

	gwdata->read_count += 1;
	return (0);
}

/* Special modg.  This is a fast implementation of mod k*2^n+c using just */
/* shifts, adds, and divide and mul by small numbers.  All others moduli */
/* call the slow giants code. */

void specialmodg (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	giant	g)
{
	int	neg, count;
	giant	n;

/* If the modulus is a general-purpose number, then let the giants code do the work.  This is done for both GENERAL_MOD and (k*b^n+c)/d cases. */

	if (gwdata->GW_MODULUS != NULL) {
		modgi (&gwdata->gdata, gwdata->GW_MODULUS, g);
		return;
	}

/* A quick check to see if the input number is between 0 and k*b^n+c.  This will save us some slow giants calls. */

	if (g->sign == 0) return;
	if (g->sign == 1 && log2 (1.00001 * (double) g->n[0]) < gwdata->bit_length) return;
	if (g->sign >= 2 && log2 (1.00001 * (g->n[g->sign-1] * 4294967296.0 + g->n[g->sign-2])) + 32 * (g->sign - 2) < gwdata->bit_length) return;

/* Calculate the modulo number - k*b^n+c */

	n = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 2);
	ultog (gwdata->b, n);
	power (n, gwdata->n);
	dblmulg (gwdata->k, n);
	iaddg (gwdata->c, n);

/* If b is not 2 let the giants code do the work. */

	if (gwdata->b != 2) {
		modgi (&gwdata->gdata, n, g);
		pushg (&gwdata->gdata, 1);
		return;
	}

/* Do the quick modulus code twice because in the case where */
/* abs(c) > k once won't get us close enough. */

	neg = FALSE;
	for (count = 0; count < 2; count++) {

/* Handle negative input values */

	    neg ^= (g->sign < 0);
	    g->sign = abs (g->sign);

/* If number is bigger than the modulus, do a mod using shifts and adds */
/* This will get us close to the right answer. */

	    if (gcompg (g, n) > 0) {
		giant	tmp1;

/* Allocate temporary */

		tmp1 = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 5);

/* Calculate the modulo by dividing the upper bits of k, multiplying by */
/* c and subtracting that from the bottom bits. */

		gtogshiftright (gwdata->n, g, tmp1);	// Upper bits
		gmaskbits (gwdata->n, g);		// Lower bits

		if (gwdata->k == 1.0) {
			imulg (gwdata->c, tmp1);	// Upper bits times C
			subg (tmp1, g);
		} else {
			giant	tmp2, tmp3;

			tmp2 = popg (&gwdata->gdata, (((unsigned long) gwdata->bit_length >> 5) + 5) * 2);
			tmp3 = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 5);

			gtog (tmp1, tmp2);
			dbltog (gwdata->k, tmp3);
			divgi (&gwdata->gdata, tmp3, tmp1);	// Upper bits over K
			mulgi (&gwdata->gdata, tmp1, tmp3);
			subg (tmp3, tmp2);	// Upper bits mod K

			gshiftleft (gwdata->n, tmp2);
			addg (tmp2, g);		// Upper bits mod K+lower bits

			imulg (gwdata->c, tmp1);// Upper bits over K times C
			subg (tmp1, g);
			pushg (&gwdata->gdata, 2);
		}

		pushg (&gwdata->gdata, 1);
	    }
	}

/* Add or subtract n until the g is between 0 and n-1 */

	while (g->sign < 0) addg (n, g);
	while (gcompg (g, n) >= 0) subg (n, g);

/* If input was negative, return k*b^n+c - g */

	if (neg && g->sign) {
		g->sign = -g->sign;
		addg (n, g);
	}

/* Free memory */

	pushg (&gwdata->gdata, 1);
}

/* Test if a gwnum is zero.  This routine was originally written by Jean Penne. */
/* It has not been adequately tested and MAY NOT BE BUG-FREE.  Use at your own risk! */

int gwiszero (
	gwhandle *gwdata,
	gwnum 	gg)
{
	unsigned long j;
	int 	result, count;

	ASSERTG (unnorms (gg) >= 0.0f);
	ASSERTG (FFT_state (gg) == NOT_FFTed);

/* If the input number is the result of an unnormalized addition or subtraction, then we had better normalize the number! */

	if (unnorms (gg) > 0.0f) {
		struct gwasm_data *asm_data;
		gwnum	gwnorm;

		gwnorm = gwalloc (gwdata);
		if (gwnorm == NULL) return (-GWERROR_MALLOC);
		dbltogw (gwdata, 0.0, gwnorm);
		asm_data = (struct gwasm_data *) gwdata->asm_data;
		asm_data->SRCARG = gg;
		asm_data->SRC2ARG = gwnorm;
		asm_data->DESTARG = gwnorm;
		gw_add (gwdata, asm_data);
		result = gwiszero (gwdata, gwnorm);
		gwfree (gwdata, gwnorm);
		return (result);
	}

/* CONCERN!!!  Could the result of a normalized multiply be greater than k*b^n+c? */
/* If so, we should test the top FFT word and if it is bigger than the maximum valid */
/* value, do a normalizing add identical to the code above. */	

/* Look through all the FFT data.  If each FFT word is zero, then the gwnum is zero. */
/* If we run into just a few non-zero FFT elements, then the gwnum might still be zero */
/* because of the way carries are propagated in the assembly code.  If we run into */
/* a large number of non-zero FFT words then the gwnum is not zero. */

#define	MAX_NZ_COUNT 16
	count = 0;
	for (j = 0; j < gwdata->FFTLEN; j++) {
		double	*valaddr = addr (gwdata, gg, j);
		if (! is_valid_double_addr (valaddr)) return (GWERROR_BAD_FFT_DATA);
		if (*valaddr == 0.0) continue;
		if (++count > MAX_NZ_COUNT) return (FALSE);	// Too many non zero words, the gwnum is not zero.
	}
	if (count) {			// The gwnum may be zero but needs a more accurate test...
		giant	gtest;
		gtest = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 5) + 5);
		if (gtest == NULL) return (-GWERROR_MALLOC);
		gwtogiant (gwdata, gg, gtest);
		result = isZero (gtest);
		pushg (&gwdata->gdata, 1);
		return (result);
	}
	else
		return (TRUE);			// The gwnum is zero
}

/* Test two gwnums for equality.  Written by Jean Penne.  Uses the gwiszero routine which MAY NOT BE BUG-FREE.  Use this routine at your own risk. */

int gwequal (
	gwhandle *gwdata,
	gwnum	gw1,
	gwnum	gw2)
{
	gwnum	gwdiff;
	int	result;

	ASSERTG (unnorms (gw1) >= 0.0f);
	ASSERTG (unnorms (gw2) >= 0.0f);
	ASSERTG (FFT_state (gw1) == NOT_FFTed);
	ASSERTG (FFT_state (gw2) == NOT_FFTed);

/* Allocate memory for the difference */

	gwdiff = gwalloc (gwdata);

/* Do a normalized subtract */

	gwsub3o (gwdata, gw1, gw2, gwdiff, GWADD_FORCE_NORMALIZE);

/* The two input numbers are equal if the difference is zero */

	result = gwiszero (gwdata, gwdiff);

/* Cleanup and return result */

	gwfree (gwdata, gwdiff);
	return (result);
}

/******************************************************************/
/* Wrapper routines for the multiplication assembly code routines */
/******************************************************************/

/* Init GW_FFT1 for FMA operations */

void init_FFT1 (		/* Calculate GW_FFT1 if necessary */
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{

/* If this is a cloned gwdata, see if we can save some memory by using the parent GW_FFT1 */

	if (gwdata->clone_of != NULL && gwdata->GW_FFT1 != gwdata->clone_of->GW_FFT1 && gwdata->clone_of->GW_FFT1 != NULL) {
		gwfree (gwdata, gwdata->GW_FFT1);
		gwdata->GW_FFT1 = gwdata->clone_of->GW_FFT1;
	}

/* Check if GW_FFT1 has already been initialized (FFT1_state = 1) or GW_FFT1 is not needed becasue it is all ones (FFT_state = 2) */

	else if (gwdata->FFT1_state != 0) return;

/* Allocate and calculate FFT(1) */

	else if (gwdata->GW_FFT1 == NULL) {		// FFT(1) may already be allocated at user's request
		gwnum fft1 = gwalloc_internal (gwdata);
		dbltogw (gwdata, 1.0, fft1);
		gwfft (gwdata, fft1, fft1);
		gwdata->GW_FFT1 = fft1;			// For gwclone's sake, only set GW_FFT1 once it is fully initialized
	}
	gwdata->FFT1_state = 1;
}

/* Init GW_FFT1 at user's request */

void gwuser_init_FFT1 (		/* Calculate GW_FFT1 at user's request */
	gwhandle *gwdata)	/* Handle initialized by gwsetup */
{
	init_FFT1 (gwdata);
	gwdata->FFT1_user_allocated = 1;
}

/* Internal routine to implement gwmul3 by calling fft/multiply assembly code. */
/* Caller must set ffttype, SRC2ARG, DEST2ARG, mul4_opcode, NORMRTN prior to calling this routine.  Caller is responsible for emulate_mod. */

void asm_mul (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;

/* Set the flag which controls whether the multiply code should begin the forward FFT of the results of a multiply. */
/* This is a little faster than than doing a full forward FFT later on.  The downside is the caller cannot convert the */
/* results of the multiply to an integer without an inefficient call to gwunfft. */

	gwdata->POSTFFT = ((options & GWMUL_STARTNEXTFFT) && !gwdata->GENERAL_MOD && gwdata->PASS1_SIZE);
	// Copy POSTFFT state to asm_data for x87 FFTs
#ifndef X86_64
	if (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2))) asm_data->u.x87.POSTFFT = gwdata->POSTFFT;
#endif	

/* Set/clear the addin value depending on GWMUL_ADDINCONST option */

	asm_data->ADDIN_VALUE = (options & GWMUL_ADDINCONST) ? gwdata->asm_addin_value : 0.0;
	asm_data->POSTADDIN_VALUE = (options & GWMUL_ADDINCONST) ? gwdata->asm_postaddin_value : 0.0;

/* Call the assembly code */

	asm_data->DESTARG = d;
	asm_data->DIST_TO_FFTSRCARG = (intptr_t) s1 - (intptr_t) d;
	asm_data->DIST_TO_MULSRCARG = (intptr_t) s2 - (intptr_t) d;

	asm_data->zero_fft = (asm_data->NORMRTN == gwdata->GWPROCPTRS[zerohigh_routines + gwdata->ERROR_CHECKING]);
	asm_data->const_fft = !!(options & GWMUL_MULBYCONST);
	asm_data->add_sub_smallmul_op = 0;
//if (rand() % 100 < 1) *s += 1.0;			// Generate random errors (for caller to test error recovery)
//if (rand() % 1000 < 2) *s += 1.0E200 * 1.0E200;
	gw_fft (gwdata, asm_data);
//if (rand() % 100 < 1) *d += 1.0;			// Generate random errors (for caller to test error recovery)
//if (rand() % 1000 < 2) *d += 1.0E200 * 1.0E200;
	if (! is_valid_double_addr (&gwsumout (gwdata, d))) gwdata->GWERROR |= 1;
	gwdata->fft_count += (asm_data->ffttype <= 3) ? 2 : 1;

/* For the two destination case, mark the source fully FFTed and calculate the address for the multiplication destination */

	if (asm_data->DEST2ARG) {
		FFT_state (s1) = FULLY_FFTed;
		d = (gwnum) ((char *) d + (intptr_t) asm_data->DEST2ARG);
	}

/* Reset the unnormalized add count, set the FFT state */

	unnorms (d) = 0.0f;
	FFT_state (d) = gwdata->POSTFFT ? PARTIALLY_FFTed : NOT_FFTed;
}

/* Internal routine to implement gwmul3 by calling fft/multiply assembly code. */
/* Caller must set NORMRTN prior to calling this routine.  Caller is responsible for emulate_mod. */

void raw_gwmul3 (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
#define swaps1s2() { gwswap (s1, s2); options = (options & ~0xF) + ((options & 0x3) << 2) + ((options & 0xC) >> 2); }
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	gwnum	tmp1, tmp2;

	tmp1 = tmp2 = NULL;

/* Assume we will not be asking the assembly code to use two destinations */

	asm_data->mul4_opcode = 0;
	asm_data->DEST2ARG = 0;

/* New in 30.4, the AVX, FMA, and AVX-512 FFT routines can handle two destinations!  One for the FFT of s1 and one for the multiplication result. */

#ifdef X86_64
//	if (gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) {
// Zeropad workaround below fails if s1 partially FFTed and s2 = d.  This is a quick fix -- awaiting better solution.
if ((gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) &&
    ! (gwdata->ZERO_PADDED_FFT && FFT_state (s1) == PARTIALLY_FFTed && s2 == d) &&
    ! (gwdata->ZERO_PADDED_FFT && FFT_state (s2) == PARTIALLY_FFTed && s1 == d)) {
#else
	if (0) {
#endif

/* Handle squaring.  AVX-512 can handle two destination squaring.  AVX and FMA3 handle it through a type-3 multiply. */

		if (s1 == s2) {
			if (FFT_state (s1) == FULLY_FFTed) asm_data->ffttype = 4;
			else if (!(options & GWMUL_FFT_S12)) asm_data->ffttype = 2;
			else {	// Two destinations
				asm_data->ffttype = (gwdata->cpu_flags & CPU_AVX512F) ? 2 : 3;
				asm_data->DEST2ARG = d;
				asm_data->mul4_opcode = 0x80;
			}
		}

/* Handle multiplication.  The s2 argument must be FFTed prior to calling the assembly code.  The assembly code can FFT s1 and save the result while */
/* storing the result of the multiply in a different destination.  Decide which of s1 and s2 is better suited to being s2.  If GWMUL_FFT_S1 or GWMUL_FFT_S2 */
/* is set, it is better to make that s2 using gwfft -- that saves one write because asm_mul will not need to write to teo destinations. */

		else {
			gwnum	preferred_FFT_arg;	// Which source should be src1 in asm_mul possibly getting FFTed by asm_mul

/* If a source arg equals d then the other source args can be the type-3 forward FFT arg only if they use two destinations. */
/* Otherwise, the forward FFT of type-3 will write to d which corrupt the source arg that equals d. */

			bool s1_can_be_FFTed_and_discarded = (s2 != d);
			bool s2_can_be_FFTed_and_discarded = (s1 != d);

/* Pick which argument to FFT.  Prefer to FFT one that we can discard the FFT result (saves a write). */

			// Type-4 FFT
			if (FFT_state (s1) == FULLY_FFTed && FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s1;
			// Next, prefer an arg that must be preserved (saves allocating a temporary and maybe a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && (options & GWMUL_PRESERVE_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && (options & GWMUL_PRESERVE_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Next, prefer an arg that we're allowed to leave in unFFTed state (may save a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_FFT_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_FFT_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Handle must FFT case using two destinations (may save a read)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S1)) preferred_FFT_arg = s1, asm_data->DEST2ARG = d;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S2)) preferred_FFT_arg = s2, asm_data->DEST2ARG = d;
			// Rarely happens.  Preserve option forces us to use gwalloc, gwfft, and a type-4 multiply
			else if (FFT_state (s1) == FULLY_FFTed) preferred_FFT_arg = s1;
			else if (FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s2;
			else ASSERTG (FALSE);

/* Pre-FFT all but the preferred FFT arg.  Even the preferred FFT arg may need to be FFTed. */

			if (preferred_FFT_arg != s1 && FFT_state (s1) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
				else gwfft (gwdata, s1, s1);
			}
			if (preferred_FFT_arg != s2 && FFT_state (s2) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
				else gwfft (gwdata, s2, s2);
			}

/* Determine the type of FFT multiply, rearrange sources so that preferred_FFT_arg is always the first arg passed to assembly code */

			if (FFT_state (preferred_FFT_arg) == FULLY_FFTed) asm_data->ffttype = 4;
			else {
				asm_data->ffttype = 3;
				if (preferred_FFT_arg == s2) {
					s2 = s1;
					s1 = preferred_FFT_arg;
				}
			}
		}
	}

/* Handle the original style single-destination FFT routines. */

	else {

/* Handle squaring */

		if (s1 == s2) {
			if (FFT_state (s1) == FULLY_FFTed) asm_data->ffttype = 4;
			else if (options & GWMUL_FFT_S12) gwfft (gwdata, s1, s1), asm_data->ffttype = 4;
			else asm_data->ffttype = 2;
		}

/* Handle multiplication.  If one source is the same as destination, make it s1.  We will always FFT s2 before calling the assembly code. */
/* Decide which of s1 and s2 is better suited to being s2. */

		else {
			if (s2 == d) { swaps1s2 (); }						// Only s1 can equal d
			else if (s1 == d);							// Only s1 can equal d
			else if (FFT_state (s2) == FULLY_FFTed || (options & GWMUL_FFT_S2));	// s2 is already a good choice for 2nd argument
			else if (FFT_state (s1) == FULLY_FFTed || (options & GWMUL_FFT_S1)) { swaps1s2 (); } // s1 is the better choice for 2nd arg
			// Neither is already FFTed or must be FFTed.  We have a choice -- next preference is scratch variables
			else if	(!(options & GWMUL_PRESERVE_S2));				// s2 need not be preserved, overwrite it with FFT
			else if	(!(options & GWMUL_PRESERVE_S1)) { swaps1s2 (); }		// s1 need not be preserved, overwrite it with FFT
			// Ugh, both must be preserved, a temporary will be required

			// Make sure s2 is FFTed.
			if (FFT_state (s2) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
				else gwfft (gwdata, s2, s2);
			}

			if (FFT_state (s1) == FULLY_FFTed) asm_data->ffttype = 4;
			else if (options & GWMUL_FFT_S1) gwfft (gwdata, s1, s1), asm_data->ffttype = 4;
			else asm_data->ffttype = 3;
		}
	}

/* Work around zero-padding bug.  If s1 is partially FFTed and s1 != d, then xmult7/ymult7/zmult7 all read d's 7 saved values rather than s1's. */

	if (gwdata->ZERO_PADDED_FFT && FFT_state (s1) == PARTIALLY_FFTed && s1 != d) memcpy (&d[-11], &s1[-11], 7 * sizeof (double));

/* Pre-adjust counts for input that is not partially or fully FFTed */

	if (FFT_state (s1) == NOT_FFTed) gwdata->read_count += 1, gwdata->write_count += 1;

/* Call the assembly code.  When there are 2 destinations start with the FFT destination */

	if (asm_data->DEST2ARG) {
		asm_data->mul4_opcode = 0x80;
		asm_data->DEST2ARG = (void *) ((intptr_t) asm_data->DEST2ARG - (intptr_t) s1);  // Make DEST2ARG an offset from s1
		asm_mul (gwdata, s1, s2, s1, options);
	} else {
		asm_data->mul4_opcode = 0;
		asm_mul (gwdata, s1, s2, d, options);
	}

/* Post-adjust counts */

	gwdata->read_count += 3, gwdata->write_count += 2;
	if (s1 == s2) gwdata->read_count -= 1;
	if (asm_data->DEST2ARG) gwdata->write_count += 1;

/* Free temporary memory */

	if (tmp1 != NULL) gwfree (gwdata, tmp1);
	if (tmp2 != NULL) gwfree (gwdata, tmp2);
}

/* Common code to emulate the modulo with two multiplies in the general purpose case */

void emulate_mod (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s)		/* Source and destination */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	gwnum	tmp;

	ASSERTG (* addr (gwdata, s, gwdata->FFTLEN-1) > -2.0 && * addr (gwdata, s, gwdata->FFTLEN-1) <= 0.0);
	if (* addr (gwdata, s, gwdata->FFTLEN-1) <= -2.0 || * addr (gwdata, s, gwdata->FFTLEN-1) > 0.0)
		gwdata->GWERROR = GWERROR_INTERNAL + 10;

/* Copy the number and zero out the low words. */

	tmp = gwalloc (gwdata);
	gwcopyzero (gwdata, s, tmp, gwdata->GW_ZEROWORDSLOW);

/* Multiply by the reciprocal that has been carefully shifted so that the integer part of the result wraps to the lower FFT words. */
/* Adjust the normalization routine so that the FFT code zeroes the high FFT words and we are left with just the quotient! */

	asm_data->NORMRTN = gwdata->GWPROCPTRS[zerohigh_routines + gwdata->ERROR_CHECKING];
	raw_gwmul3 (gwdata, gwdata->GW_RECIP_FFT, tmp, tmp, GWMUL_PRESERVE_S1);
	ASSERTG (* addr (gwdata, tmp, gwdata->FFTLEN/2-1) > -2.0 && * addr (gwdata, tmp, gwdata->FFTLEN/2-1) <= 0.0);
	if (* addr (gwdata, tmp, gwdata->FFTLEN/2-1) <= -2.0 || * addr (gwdata, tmp, gwdata->FFTLEN/2-1) > 0.0)
		gwdata->GWERROR = GWERROR_INTERNAL + 11;

/* Muliply quotient and modulus.  Select normalization routine that does not zero the high FFT words and preserves roundoff checking. */

	asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
	raw_gwmul3 (gwdata, gwdata->GW_MODULUS_FFT, tmp, tmp, GWMUL_PRESERVE_S1);

/* Subtract from the original number to get the remainder.  Callers of gwmul3 expect results to be fully normalized, especially if they are carefully */
/* counting unnormalized adds.  Thus, it might be prudent to always normalize this subtraction.  However, Pavel Atnashev reports that a normalized subtract */
/* greatly slows down multi-threaded emulate_mod.  So I tried an unnormalized subtract with clearing the unnorm count.  QA shows the roundoff error does */
/* not get out of hand. */
/* NOTE: a 2023-02-05 change to AVX normalization caused emulate_mod failures on tiny FFTs (example: 3*2^305+1).  A 2023-02-27 re-fix seems to have corrected this. */
/* Thus, the interim fix that does normalized adds for small FFTs which cannot be multi-threaded anyway is commented out. */

	//	if (gwdata->FFTLEN <= 1024) {
	//	gwsub3o (gwdata, s, tmp, s, GWADD_FORCE_NORMALIZE);
	//} else {
		gwsub3 (gwdata, s, tmp, s);
		unnorms (s) = 0.0f;
	//}

// All done.  Cleanup and return.  The upper half of FFT data should now be all zeroes.  Unfortunately, the unnormalized subtract above means there can be
// pairs of (2^num_b_per_small_word, -1) anywhere in the upper half.  These pairs are harmless, but means the ASSERT below does not work.
// ASSERTG (* addr (gwdata, s, gwdata->FFTLEN/2) == 0.0);

	ASSERTG (* addr (gwdata, s, gwdata->FFTLEN-1) == 0.0);
	gwfree (gwdata, tmp);
}

/* Perform an inverse FFT.  This may be inefficient!!  Call this to undo a forward FFT performed on a gwnum where unFFTed data is needed. */
/* In a perfect world, the forward FFT would not have been done in the first place.  However, there are counter examples.  Plus, the new */
/* polymult library returns FFTed data that needs normalizing before it can be used in future operations.  In fact, polymult users may */
/* benefit from using the GWMUL_STARTNEXTFFT option! */

void gwunfft2 (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d,		/* Destination (can equal source) */
	int	options)	/* Any of GWMUL_ADDINCONST, GWMUL_MULBYCONST, GWMUL_STARTNEXTFFT */
{
	int	save_count;

	ASSERTG (unnorms (s) >= 0.0f);

/* Handle the easy case */

	if (FFT_state (s) == NOT_FFTed) {
		ASSERTG (! (options & (GWMUL_ADDINCONST | GWMUL_MULBYCONST)));		/* We could figure out how to emulate this if ever needed */
		if (s != d) gwcopy (gwdata, s, d);
		return;
	}

/* MMGW general mod is easy.  Undo what gwfft did, simply unfft the cyclic FFT data. */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwunfft2 (gwdata->cyclic_gwdata, cyclic_gwnum (gwdata, s), cyclic_gwnum (gwdata, d), options);
		return;
	}

/* Since cmn_mulop4 calls this routine when emulating, clear the careful count to prevent infinite recursion. */

	save_count = gwdata->careful_count;
	gwdata->careful_count = 0;

/* Clear unnorms so if we call gwmul3 it won't assert when polymult has set unnorms very high. */

	unnorms (s) = 0.0f;

/* For partially or fully FFTed data, multiply source by 1.0 */

	if (FFT_state (s) == PARTIALLY_FFTed || FFT_state (s) == FULLY_FFTed) {

		// In modern 64-bit architectures, if FFT(1) is not all ones then use a cached FFT(1) to do the unfft.
		// In older 64-bit architectures, use a cached FFT(1) to do the unfft even if FFT(1) is all ones.
		init_FFT1 (gwdata);
		if (gwdata->GW_FFT1 != NULL) {
			gwmul3 (gwdata, s, gwdata->GW_FFT1, d, options);
		}

		// Special faster unfft code for most 64-bit architectures that does not use a saved FFT(1) because it is all ones
#ifdef X86_64
		else if (gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) {
			ASSERTG (gwdata->k == 1.0);
			struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
			if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
			else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
			asm_data->mul4_opcode = 5;
			asm_data->SRC3ARG = (void *) 1;			// Indicate this gwnum is not FFTed_FOR_FMA and FFT(1) is one
			asm_data->DEST2ARG = 0;
			if (FFT_state (s) == FULLY_FFTed) asm_data->ffttype = 4;
			else asm_data->ffttype = 3;
			asm_mul (gwdata, s, s, d, options);
			gwdata->read_count += 2, gwdata->write_count += 2;
			if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
		}
#endif

		// Emulate unfft by multiplying with a gwnum containing one
		else if (s != d) {
			dbltogw (gwdata, 1.0, d);
			gwmul3 (gwdata, s, d, d, options);
		} else {
			gwnum	tmp = gwalloc (gwdata);
			dbltogw (gwdata, 1.0, tmp);
			gwmul3 (gwdata, s, tmp, d, options);
			gwfree (gwdata, tmp);
		}
	}

/* For FFTed-for-FMA data which is output by the polymult library when k != 1, we want a fairly efficient solution. */

	if (FFT_state (s) == FFTed_FOR_FMA) {
		ASSERTG (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0);

		// For MMGW mod, do a reduction
		if (gwdata->GENERAL_MMGW_MOD) {
			if (s != d) gwcopy (gwdata, s, d);				// Should never happen (we could modify and restore FFT_state(s) instead)
			FFT_state (cyclic_gwnum (gwdata, d)) = FULLY_FFTed;		// Change FFT state for the recursive calls to gwunfft2
			FFT_state (negacyclic_gwnum (gwdata, d)) = FULLY_FFTed;
			unnorms (negacyclic_gwnum (gwdata, d)) = unnorms (cyclic_gwnum (gwdata, d));
			ASSERTG (! (options & (GWMUL_MULBYCONST | GWMUL_ADDINCONST)));
			gwunfft2 (gwdata->cyclic_gwdata, cyclic_gwnum (gwdata, s), cyclic_gwnum (gwdata, d), GWMUL_STARTNEXTFFT);
			MMGW_redc4 (gwdata, cyclic_gwnum (gwdata, d), negacyclic_gwnum (gwdata, d));
		}

		// Special faster unfft code for most 64-bit architectures that does not need any extra variables.
		// We can use the same asm unfft code as above since a FFTed_FOR_FMA value has already been multiplied by FFT(1).
#ifdef X86_64
		else if (gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) {
			struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
			if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
			else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
			asm_data->mul4_opcode = 5;
			asm_data->SRC3ARG = (void *) 2;			// Indicate this gwnum is FFTed_FOR_FMA and FFT(1) is not one
			asm_data->DEST2ARG = 0;
			asm_data->ffttype = 4;
			asm_mul (gwdata, s, s, d, options);
			gwdata->read_count += 2, gwdata->write_count += 2;
			if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
		}
#endif
		// Slower code for CPUs we no longer go out of our way to optimize.  FFT(k) should be all ones, so multiplying by k will not affect
		// the implied mul-by-FFT(1) that is included in all FFTed_FOR_FMA values.
		else {
			gwnum	tmp = gwalloc (gwdata);
			dbltogw (gwdata, gwdata->k, tmp);		// should be all ones when FFTed
			gwfft (gwdata, tmp, tmp);
			if (gwdata->ZERO_PADDED_FFT) {	// trick zmult7/ymult7/xmult7 into copying the 7 ZPAD header values that polymult already computed
				if (gwdata->cpu_flags & CPU_AVX512F) tmp[-5] = 1.0;
				else tmp[-8] = 1.0;
			}

			struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
			if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
			else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
			asm_data->mul4_opcode = 0;
			asm_data->DEST2ARG = 0;
			asm_data->ffttype = 4;
			asm_mul (gwdata, tmp, s, d, options);
			gwdata->read_count += 2, gwdata->write_count += 2;
			if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);

			gwfree (gwdata, tmp);
		}
	}

/* Restore state */

	gwdata->careful_count = save_count;
}

/* Perform an inverse FFT.  This may be inefficient!!  Call this to undo a forward FFT performed on a gwnum where unFFTed data is needed. */
/* In a perfect world, the forward FFT would not have been done in the first place.  However, there are counter examples.  Plus, the new */
/* polymult library returns FFTed data that needs normalizing before it can be used in future operations. */

void gwunfft (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d)		/* Destination (can overlap source) */
{
	gwunfft2 (gwdata, s, d, 0);	/* Perform an unfft with no mul-by-const, add-in, or start-next-FFT options */
}

/* Special multiply routine that adds/subtracts two numbers and multiplies by a third. */

void cmn_gwopmul4 (		/* Calculate (s1 op s2) * s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options,	/* See gwnum.h */
	int	opcode)		/* 1=ADD or 2=SUBTRACT */
{
	gwnum	tmp1, tmp2, tmp3;

	tmp1 = tmp2 = tmp3 = NULL;

/* Clear flags that are impossible when a source is the same as the destination */

	if (s1 == d) options &= ~(GWMUL_FFT_S1 | GWMUL_PRESERVE_S1);
	if (s2 == d) options &= ~(GWMUL_FFT_S2 | GWMUL_PRESERVE_S2);
	if (s3 == d) options &= ~(GWMUL_FFT_S3 | GWMUL_PRESERVE_S3);

/* Make options consistent when two sources point to the same gwnum.  Caller might have lazily set the FFT or PRESERVE option for just one of the sources. */

	if (s1 == s3) options |= ((options & (GWMUL_FFT_S1 | GWMUL_PRESERVE_S1)) << 4) | ((options & (GWMUL_FFT_S3 | GWMUL_PRESERVE_S3)) >> 4);
	if (s2 == s3) options |= ((options & (GWMUL_FFT_S2 | GWMUL_PRESERVE_S2)) << 2) | ((options & (GWMUL_FFT_S3 | GWMUL_PRESERVE_S3)) >> 2);

/* See if GWMUL_ADDINCONST must be emulated */

	if ((options & GWMUL_ADDINCONST) && (gwdata->emulate_addin_value != 0 || gwdata->emulate_postaddin_value != 0)) {
		cmn_gwopmul4 (gwdata, s1, s2, s3, d, options & ~(GWMUL_MULBYCONST | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT), opcode);
		init_GW_ADDIN (gwdata);
		if (gwdata->GW_ADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_ADDIN, d, GWADD_FORCE_NORMALIZE);
		if (options & GWMUL_MULBYCONST) gwsmallmul (gwdata, gwdata->mulbyconst, d);
		if (gwdata->GW_POSTADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_POSTADDIN, d, GWADD_FORCE_NORMALIZE);
	}

/* Handle general mod with Montgomery-McLaughlin-Gallot-Woltman multiplication and reduction */

	else if (gwdata->GENERAL_MMGW_MOD) {
		// Apply request to FFT input arguments.  FUTURE: It might be possible to use two-destination FFTs in some cases.
		if (FFT_state (s1) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
			else gwfft (gwdata, s1, s1);
		}
		if (FFT_state (s2) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
			else gwfft (gwdata, s2, s2);
		}
		if (FFT_state (s3) != FULLY_FFTed && options & GWMUL_FFT_S3) gwfft (gwdata, s3, s3);

		// Get pointers to the eight gwnums
		gwnum s1R = cyclic_gwnum (gwdata, s1);
		gwnum s1Q = negacyclic_gwnum (gwdata, s1);
		gwnum s2R = cyclic_gwnum (gwdata, s2);
		gwnum s2Q = negacyclic_gwnum (gwdata, s2);
		gwnum s3R = cyclic_gwnum (gwdata, s3);
		gwnum s3Q = negacyclic_gwnum (gwdata, s3);
		gwnum T_R = cyclic_gwnum (gwdata, d);	
		gwnum T_Q = negacyclic_gwnum (gwdata, d);

		// Compute FFT of s3 where necessary
		if (FFT_state (s3R) != FULLY_FFTed) {					// If s3R is FFTed, so is s3Q
			if (s3 != d && !(options & GWMUL_PRESERVE_S3) && (s1 == d || s2 == d))
				gwfft (gwdata, s3, s3);					// FFT both s3R and s3Q as a pair for possible future use
			else
				s3Q = s3R;						// Delay the FFT for the gwmul3 that computes T_Q
		}

		// T_Q = s1Q * s2Q +/- s3Q mod Q
		cmn_gwopmul4 (gwdata->negacyclic_gwdata, s1Q, s2Q, s3Q, T_Q, options & ~GWMUL_STARTNEXTFFT, opcode);

		// T_R = s1R * s2R +/- s3R mod R
		cmn_gwopmul4 (gwdata->cyclic_gwdata, s1R, s2R, s3R, T_R, options | GWMUL_STARTNEXTFFT, opcode);

		// Reduction.  Multiply T_R by 2/N.
		MMGW_redc (gwdata, T_R, T_Q);
	}

/* If asm code cannot handle this feature, or the multiply carefully count is active, then emulate this function */

#ifdef X86_64
	else if (!(gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) || gwdata->careful_count > 0) {
#else
	else if (1) {
#endif
		tmp1 = gwalloc (gwdata);
		if (opcode == 1) gwadd3o (gwdata, s1, s2, tmp1, GWADD_DELAY_NORMALIZE);
		else gwsub3o (gwdata, s1, s2, tmp1, GWADD_DELAY_NORMALIZE);
		gwmul3 (gwdata, tmp1, s3, d, (options & ~0x3F) + ((options & 0x30) >> 2));	// Shift s3 options down to s2
	}

/* Let assembly code do the work */

	else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
		gwnum	preferred_FFT_arg;

/* Assume we will not be asking the assembly code to use two destinations */

		asm_data->DEST2ARG = 0;

/* If a source arg equals d then the other source args can be the type-3 forward FFT arg only if they use two destinations. */
/* Otherwise, the forward FFT of type-3 will write to d which corrupt the source arg that equals d. */

		bool s1_can_be_FFTed_and_discarded = (s2 != d && s3 != d);
		bool s2_can_be_FFTed_and_discarded = (s1 != d && s3 != d);
		bool s3_can_be_FFTed_and_discarded = (s1 != d && s2 != d);

/* Pick which argument to FFT.  Prefer to FFT one that we can discard the FFT result (saves a write). */
/* NOTE: Type-4 FFTs do not support rearranging source arguments. */
/* NOTE: Having s1 equal s2 is a silly way to mul-by-two (add) or generate a zero (subtract).  The asm code only handles this if s1 and s2 are pre-FFTed. */

		// Type-4 FFT and bizarre s1 = s2 case
		if ((FFT_state (s1) == FULLY_FFTed && FFT_state (s2) == FULLY_FFTed && FFT_state (s3) == FULLY_FFTed) || s1 == s2) preferred_FFT_arg = s3;
		// Next, prefer an arg that must be preserved (saves allocating a temporary and maybe a read-write)
		else if (FFT_state (s3) != FULLY_FFTed && (options & GWMUL_PRESERVE_S3) && s3_can_be_FFTed_and_discarded) preferred_FFT_arg = s3;
		else if (FFT_state (s1) != FULLY_FFTed && (options & GWMUL_PRESERVE_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
		else if (FFT_state (s2) != FULLY_FFTed && (options & GWMUL_PRESERVE_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
		// Next, prefer an arg that we're allowed to leave in unFFTed state (may save a read-write)
		else if (FFT_state (s3) != FULLY_FFTed && !(options & GWMUL_FFT_S3) && s3_can_be_FFTed_and_discarded) preferred_FFT_arg = s3;
		else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_FFT_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
		else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_FFT_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
		// Handle must FFT case using two destinations (may save a read)
		else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S1)) preferred_FFT_arg = s1, asm_data->DEST2ARG = d;
		else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S2)) preferred_FFT_arg = s2, asm_data->DEST2ARG = d;
		else if (FFT_state (s3) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S3)) preferred_FFT_arg = s3, asm_data->DEST2ARG = d;
		// Rarely happens.  Preserve option forces us to use gwalloc, gwfft, and a type-4 multiply
		else if (FFT_state (s1) == FULLY_FFTed) preferred_FFT_arg = s1;
		else if (FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s2;
		else if (FFT_state (s3) == FULLY_FFTed) preferred_FFT_arg = s3;
		else ASSERTG (FALSE);

/* Pre-FFT all but the preferred FFT arg.  Even the preferred FFT arg may need to be FFTed. */

		if (preferred_FFT_arg != s1 && FFT_state (s1) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
			else gwfft (gwdata, s1, s1);
		}
		if (preferred_FFT_arg != s2 && FFT_state (s2) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
			else gwfft (gwdata, s2, s2);
		}
		if (preferred_FFT_arg != s3 && FFT_state (s3) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S3) { tmp3 = gwalloc (gwdata); gwfft (gwdata, s3, tmp3); s3 = tmp3; }
			else gwfft (gwdata, s3, s3);
		}

/* Determine the type of FFT multiply and generate the mul4_opcode, rearrange sources so that preferred_FFT_arg is always the first arg passed to assembly code */

		if (FFT_state (preferred_FFT_arg) == FULLY_FFTed) asm_data->ffttype = 4;
		else {
			asm_data->ffttype = 3;
			if (preferred_FFT_arg == s1) {
				opcode = (opcode-1) * 4 + ((preferred_FFT_arg == s3) ? 7 : 6);		// addmul313 or addmul312 or submul313 or submul312
				s1 = s2;
				s2 = s3;
				s3 = preferred_FFT_arg;
			}
			else if (preferred_FFT_arg == s2) {
				opcode = (opcode-1) * 4 + ((preferred_FFT_arg == s3) ? 9: 8);		// addmul133 or addmul132 or submul133 or submul132
				s2 = s3;
				s3 = preferred_FFT_arg;
			}
		}

/* Pre-adjust counts for input that is not partially or fully FFTed */

		if (FFT_state (s3) == NOT_FFTed) gwdata->read_count += 1, gwdata->write_count += 1;

/* Call the assembly code */

		if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
		else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
		if (asm_data->DEST2ARG) {
			asm_data->mul4_opcode = 0x80 | opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s2 - (intptr_t) s3);
			asm_data->DEST2ARG = (void *) ((intptr_t) asm_data->DEST2ARG - (intptr_t) s3);
			asm_mul (gwdata, s3, s1, s3, options);
		} else {
			asm_data->mul4_opcode = opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s2 - (intptr_t) d);
			asm_mul (gwdata, s3, s1, d, options);
		}

/* Post-adjust counts */

		gwdata->read_count += 4, gwdata->write_count += 2;
		if (s1 == s2 || s1 == s3) gwdata->read_count -= 1;
		if (s2 == s3) gwdata->read_count -= 1;
		if (asm_data->DEST2ARG) gwdata->write_count += 1;

/* Emulate mod with 2 multiplies case */

		if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
	}

/* Free temporary memory */

	if (tmp1 != NULL) gwfree (gwdata, tmp1);
	if (tmp2 != NULL) gwfree (gwdata, tmp2);
	if (tmp3 != NULL) gwfree (gwdata, tmp3);
}

/* Special multiply routine that multiplies two numbers and adds/subtracts a the product of a third and fourth number. */

void cmn_gwmulmulop5 (		/* Calculate (s1 * s2) op (s3 * s4) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	s4,		/* Fourth source */
	gwnum	d,		/* Destination */
	int	options,	/* See gwnum.h */
	int	opcode)		/* 3=ADD or 4=SUBTRACT */
{
	gwnum	tmp1, tmp2, tmp3, tmp4;

	tmp1 = tmp2 = tmp3 = tmp4 = NULL;

/* Clear flags that are impossible when a source is the same as the destination */

	if (s1 == d) options &= ~(GWMUL_FFT_S1 | GWMUL_PRESERVE_S1);
	if (s2 == d) options &= ~(GWMUL_FFT_S2 | GWMUL_PRESERVE_S2);
	if (s3 == d) options &= ~(GWMUL_FFT_S3 | GWMUL_PRESERVE_S3);
	if (s4 == d) options &= ~(GWMUL_FFT_S4 | GWMUL_PRESERVE_S4);

/* We may be able to save a read if adding and s1,s2 are fully FFTed and one of s3,s4 is not */

	if (opcode == 3 && FFT_state (s1) == FULLY_FFTed && FFT_state (s2) == FULLY_FFTed) {
		gwswap (s1, s3);
		gwswap (s2, s4);
	}

/* Make sure s3 and s4 are FFTed */

	if (FFT_state (s3) != FULLY_FFTed) {
		if (options & GWMUL_PRESERVE_S3) { tmp3 = gwalloc (gwdata); gwfft (gwdata, s3, tmp3); s3 = tmp3; }
		else gwfft (gwdata, s3, s3);
	}
	if (FFT_state (s4) != FULLY_FFTed) {
		if (options & GWMUL_PRESERVE_S4) { tmp4 = gwalloc (gwdata); gwfft (gwdata, s4, tmp4); s4 = tmp4; }
		else gwfft (gwdata, s4, s4);
	}

/* See if GWMUL_ADDINCONST must be emulated */

	if ((options & GWMUL_ADDINCONST) && (gwdata->emulate_addin_value != 0 || gwdata->emulate_postaddin_value != 0)) {
		cmn_gwmulmulop5 (gwdata, s1, s2, s3, s4, d, options & ~(GWMUL_MULBYCONST | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT), opcode);
		init_GW_ADDIN (gwdata);
		if (gwdata->GW_ADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_ADDIN, d, GWADD_FORCE_NORMALIZE);
		if (options & GWMUL_MULBYCONST) gwsmallmul (gwdata, gwdata->mulbyconst, d);
		if (gwdata->GW_POSTADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_POSTADDIN, d, GWADD_FORCE_NORMALIZE);
	}

/* Handle general mod with Montgomery-McLaughlin-Gallot-Woltman multiplication and reduction */

	else if (gwdata->GENERAL_MMGW_MOD) {
		int T_Q_options = options;

		// Get pointers to the ten gwnums
		gwnum s1R = cyclic_gwnum (gwdata, s1);
		gwnum s1Q = negacyclic_gwnum (gwdata, s1);
		gwnum s2R = cyclic_gwnum (gwdata, s2);
		gwnum s2Q = negacyclic_gwnum (gwdata, s2);
		gwnum s3R = cyclic_gwnum (gwdata, s3);
		gwnum s3Q = negacyclic_gwnum (gwdata, s3);
		gwnum s4R = cyclic_gwnum (gwdata, s4);
		gwnum s4Q = negacyclic_gwnum (gwdata, s4);
		gwnum T_R = cyclic_gwnum (gwdata, d);	
		gwnum T_Q = negacyclic_gwnum (gwdata, d);

		// Apply request to FFT input arguments.  FUTURE: It might be possible to use two-destination FFTs in some cases.
		if (options & GWMUL_FFT_S1) gwfft (gwdata, s1, s1);
		if (options & GWMUL_FFT_S2) gwfft (gwdata, s2, s2);

		// Different code for squaring and multiplication
		if (s1 == s2) {									// Squaring
			// Compute FFT of s1 where necessary.  Be careful with AVX's inability to do squaring without FFTing S1.
			// With some effort we could use spare Q buffers to FFT s1 for AVX machines.
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (s1 != d && !(options & GWMUL_PRESERVE_S1) && (!(gwdata->cpu_flags & CPU_AVX512F) || s3 == d || s4 == d))
					gwfft (gwdata, s1, s1);					// FFT both s1R and s1Q as a pair for possible future use
				else if (s3 != d && s4 != d) {					// Can s1 be handled with a discardable forward FFT?
					s1Q = s2Q = s1R;					// Delay the FFT for the cmn_gwmulmulop5 that computes T_Q
					T_Q_options |= GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2;
				} else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}
		} else {									// Multiplication
			// Compute FFT of s1 where necessary
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (s1 != d && !(options & GWMUL_PRESERVE_S1) && (FFT_state (s2R) != FULLY_FFTed || s2 == d || s3 == d || s4 == d))
					gwfft (gwdata, s1, s1);					// FFT both s1R and s1Q as a pair for possible future use
				else if (s2 != d && s3 != d && s4 != d)				// Can s1 be handled with a discardable forward FFT?
					s1Q = s1R, T_Q_options |= GWMUL_PRESERVE_S1;		// Delay the FFT for the mul that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}

			// Compute FFT of s2 where necessary
			if (FFT_state (s2R) != FULLY_FFTed) {					// If s2R is FFTed, so is s2Q
				if (s2 != d && !(options & GWMUL_PRESERVE_S2) && (FFT_state (s1R) != FULLY_FFTed || s1 == d || s3 == d || s4 == d))
					gwfft (gwdata, s2, s2);					// FFT both s2R and s2Q as a pair for possible future use
				else if (s1 != d && s3 != d && s4 != d && s1Q != s1R)		// Can mul discard FFT of s2?  Only one argument can be delayed.
					s2Q = s2R, T_Q_options |= GWMUL_PRESERVE_S2;		// Save a write by delaying the FFT to the mul that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s2R, s2Q);		// FFT into the currently unused s2Q buffer
			}
		}

		// T_Q = s1Q * s2Q +/- s3Q * s4Q mod Q
		cmn_gwmulmulop5 (gwdata->negacyclic_gwdata, s1Q, s2Q, s3Q, s4Q, T_Q, T_Q_options & ~GWMUL_STARTNEXTFFT, opcode);

		// T_R = s1R * s2R +/- s3R * s4R mod R
		cmn_gwmulmulop5 (gwdata->cyclic_gwdata, s1R, s2R, s3R, s4R, T_R, options | GWMUL_STARTNEXTFFT, opcode);

		// Reduction.  Multiply T_R by 2/N.
		MMGW_redc (gwdata, T_R, T_Q);
	}

/* If asm code cannot handle this feature (SSE2 and 32-bit), or the multiply carefully count is active, then emulate this feature */

#ifdef X86_64
	else if (!(gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) || gwdata->careful_count > 0) {
#else
	else if (1) {
#endif

/* Now emulate.  Be sure to do the optional mul-by-const after the FMA. */

		tmp1 = gwalloc (gwdata);
		gwmul3 (gwdata, s3, s4, tmp1, 0);
		if ((options & GWMUL_MULBYCONST) && (options & GWMUL_ADDINCONST) && gwdata->asm_postaddin_value != 0.0) {
			// Multiply without the post addin value
			double saved_postaddin_value = gwdata->asm_postaddin_value;
			gwdata->asm_postaddin_value = 0.0;
			gwmul3 (gwdata, s1, s2, d, options & ~(GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
			gwdata->asm_postaddin_value = saved_postaddin_value;
			// Add/sub the s3*s4 addin value
			if (opcode == 3) gwadd3o (gwdata, d, tmp1, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			else gwsub3o (gwdata, d, tmp1, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			// Mul by const and apply only the post addin value
			dbltogw (gwdata, gwdata->mulbyconst, tmp1);
			double saved_addin_value = gwdata->asm_addin_value;
			gwdata->asm_addin_value = 0.0;
			gwmul3 (gwdata, d, tmp1, d, options & ~GWMUL_MULBYCONST);
			gwdata->asm_addin_value = saved_addin_value;
		} else if (options & GWMUL_MULBYCONST) {
			gwmul3 (gwdata, s1, s2, d, options & ~(GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
			if (opcode == 3) gwadd3o (gwdata, d, tmp1, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			else gwsub3o (gwdata, d, tmp1, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			gwsmallmul (gwdata, gwdata->mulbyconst, d);
		} else {
			gwmul3 (gwdata, s1, s2, d, options & ~GWMUL_STARTNEXTFFT);
			if (opcode == 3) gwadd3o (gwdata, d, tmp1, d, GWADD_FORCE_NORMALIZE);
			else gwsub3o (gwdata, d, tmp1, d, GWADD_FORCE_NORMALIZE);
		}
	}

/* Let assembly code do the work */

	else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;

/* Assume we will not be asking the assembly code to use two destinations */

		asm_data->DEST2ARG = 0;

/* The AVX/FMA3 code cannot do squaring with add/sub in assembly.  Convert to type-4 by FFTing source. */
/* This is not optimal.  Should some user have a strong need for optimizing this, we can revisit upgrading the assembly code. */

		if (s1 == s2 && !(gwdata->cpu_flags & CPU_AVX512F) && FFT_state (s1) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
			else gwfft (gwdata, s1, s1);
		}

/* Handle squaring.  AVX-512 can handle two destination squaring, AVX and FMA3 cannot. */

		if (s1 == s2) {
			if (FFT_state (s1) == FULLY_FFTed) asm_data->ffttype = 4;
			else if (!(options & GWMUL_FFT_S12) && s3 != d && s4 != d) asm_data->ffttype = 2;
			else if (!(gwdata->cpu_flags & (CPU_AVX512F))) gwfft (gwdata, s1, s1), asm_data->ffttype = 4;
			else {	// Two destinations
				asm_data->ffttype = 2;
				asm_data->DEST2ARG = d;
			}
		}

/* Copy lots of code from raw_gwmul3.  The s2 argument must be FFTed prior to calling the assembly code.  However, the assembly */
/* code can FFT s1 and save the result while storing the result of the multiply in a different destination. */
/* Decide which of s1 and s2 is better suited to being s2. */

		else {
			gwnum	preferred_FFT_arg;	// Which source should be src1 in asm_mul and can be FFTed by asm_mul

/* If a source arg equals d then the other source args can be the type-3 forward FFT arg only if they use two destinations. */
/* Otherwise, the forward FFT of type-3 will write to d which corrupt the source arg that equals d. */

			bool s1_can_be_FFTed_and_discarded = (s2 != d && s3 != d && s4 != d);
			bool s2_can_be_FFTed_and_discarded = (s1 != d && s3 != d && s4 != d);

/* Pick which argument to FFT.  Prefer to FFT one that we can discard the FFT result (saves a write).  Only one of the first two sources can be preferred_FFT_arg. */

			// Type-4 FFT
			if (FFT_state (s1) == FULLY_FFTed && FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s1;
			// Next, prefer an arg that must be preserved (saves allocating a temporary and maybe a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && (options & GWMUL_PRESERVE_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && (options & GWMUL_PRESERVE_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Next, prefer an arg that we're allowed to leave in unFFTed state (may save a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_FFT_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_FFT_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Handle must FFT case using two destinations (may save a read)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S1)) preferred_FFT_arg = s1, asm_data->DEST2ARG = d;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S2)) preferred_FFT_arg = s2, asm_data->DEST2ARG = d;
			// Rarely happens.  Preserve option forces us to use gwalloc, gwfft, and a type-4 multiply
			else if (FFT_state (s1) == FULLY_FFTed) preferred_FFT_arg = s1;
			else if (FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s2;
			else ASSERTG (FALSE);

/* Pre-FFT all but the preferred FFT arg.  Even the preferred FFT arg may need to be FFTed. */

			if (preferred_FFT_arg != s1 && FFT_state (s1) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
				else gwfft (gwdata, s1, s1);
			}
			if (preferred_FFT_arg != s2 && FFT_state (s2) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
				else gwfft (gwdata, s2, s2);
			}

/* Determine the type of FFT multiply, rearrange sources so that preferred_FFT_arg is always the first arg passed to assembly code */

			if (FFT_state (preferred_FFT_arg) == FULLY_FFTed) asm_data->ffttype = 4;
			else {
				asm_data->ffttype = 3;
				if (preferred_FFT_arg == s2) {
					s2 = s1;
					s1 = preferred_FFT_arg;
				}
			}
		}

/* Pre-adjust counts for input that is not partially or fully FFTed */

		if (FFT_state (s1) == NOT_FFTed) gwdata->read_count += 1, gwdata->write_count += 1;

/* Call the assembly code */

		if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
		else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
		if (asm_data->DEST2ARG) {
			asm_data->mul4_opcode = 0x80 | opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s3 - (intptr_t) s1);
			asm_data->SRC3ARG = (void *) ((intptr_t) s4 - (intptr_t) s1);
			asm_data->DEST2ARG = (void *) ((intptr_t) asm_data->DEST2ARG - (intptr_t) s1);
			asm_mul (gwdata, s1, s2, s1, options);
		} else {
			asm_data->mul4_opcode = opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s3 - (intptr_t) d);
			asm_data->SRC3ARG = (void *) ((intptr_t) s4 - (intptr_t) d);
			asm_mul (gwdata, s1, s2, d, options);
		}

/* Post-adjust counts */

		gwdata->read_count += 5, gwdata->write_count += 2;
		if (s1 == s2 || s1 == s3 || s1 == s4) gwdata->read_count -= 1;
		if (s2 == s3 || s2 == s4) gwdata->read_count -= 1;
		if (s3 == s4) gwdata->read_count -= 1;
		if (asm_data->DEST2ARG) gwdata->write_count += 1;

/* Emulate mod with 2 multiplies */

		if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
	}

/* Free temporary memory */			

	if (tmp1 != NULL) gwfree (gwdata, tmp1);
	if (tmp2 != NULL) gwfree (gwdata, tmp2);
	if (tmp3 != NULL) gwfree (gwdata, tmp3);
	if (tmp4 != NULL) gwfree (gwdata, tmp4);
}

/* Special multiply routine that multiplies two numbers and adds/subtracts a third. */

void cmn_gwmulop4 (		/* Calculate (s1 * s2) op s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options,	/* See gwnum.h */
	int	opcode)		/* 3=ADD or 4=SUBTRACT */
{
	gwnum	tmp1, tmp2, tmp3;

	tmp1 = tmp2 = tmp3 = NULL;

/* Clear flags that are impossible when a source is the same as the destination */

	if (s1 == d) options &= ~(GWMUL_FFT_S1 | GWMUL_PRESERVE_S1);
	if (s2 == d) options &= ~(GWMUL_FFT_S2 | GWMUL_PRESERVE_S2);
	if (s3 == d) options &= ~(GWMUL_FFT_S3 | GWMUL_PRESERVE_S3);

/* Create and cache FFT(1), switch to mulmulop5 */

	if (FFT_state (s3) != FFTed_FOR_FMA) {
		init_FFT1 (gwdata);
		if (gwdata->GW_FFT1 != NULL) {
			cmn_gwmulmulop5 (gwdata, s1, s2, s3, gwdata->GW_FFT1, d, options, opcode);
			return;
		}
	}

/* See if GWMUL_ADDINCONST must be emulated */

	if ((options & GWMUL_ADDINCONST) && (gwdata->emulate_addin_value != 0 || gwdata->emulate_postaddin_value != 0)) {
		cmn_gwmulop4 (gwdata, s1, s2, s3, d, options & ~(GWMUL_MULBYCONST | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT), opcode);
		init_GW_ADDIN (gwdata);
		if (gwdata->GW_ADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_ADDIN, d, GWADD_FORCE_NORMALIZE);
		if (options & GWMUL_MULBYCONST) gwsmallmul (gwdata, gwdata->mulbyconst, d);
		if (gwdata->GW_POSTADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_POSTADDIN, d, GWADD_FORCE_NORMALIZE);
	}

/* Handle general mod with Montgomery-McLaughlin-Gallot-Woltman multiplication and reduction */

	else if (gwdata->GENERAL_MMGW_MOD) {
		int T_Q_options = options;

		// Get pointers to the eight gwnums
		gwnum s1R = cyclic_gwnum (gwdata, s1);
		gwnum s1Q = negacyclic_gwnum (gwdata, s1);
		gwnum s2R = cyclic_gwnum (gwdata, s2);
		gwnum s2Q = negacyclic_gwnum (gwdata, s2);
		gwnum s3R = cyclic_gwnum (gwdata, s3);
		gwnum s3Q = negacyclic_gwnum (gwdata, s3);
		gwnum T_R = cyclic_gwnum (gwdata, d);	
		gwnum T_Q = negacyclic_gwnum (gwdata, d);

		// Apply request to FFT input arguments.  FUTURE: It might be possible to use two-destination FFTs in some cases.
		if (options & GWMUL_FFT_S1) gwfft (gwdata, s1, s1);
		if (options & GWMUL_FFT_S2) gwfft (gwdata, s2, s2);

		// Different code for squaring and multiplication
		if (s1 == s2) {									// Squaring
			// Compute FFT of s1 where necessary.  Be careful with AVX's inability to do squaring without FFTing S1.
			// With some effort we could use spare Q buffers to FFT s1 for AVX machines.
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (s1 != d && !(options & GWMUL_PRESERVE_S1) && (!(gwdata->cpu_flags & CPU_AVX512F) || s3 == d))
					gwfft (gwdata, s1, s1);					// FFT both s1R and s1Q as a pair for possible future use
				else if (s3 != d) {						// Can s1 be handled with a discardable forward FFT?
					s1Q = s2Q = s1R;					// Delay the FFT for the cmn_gwmulop4 that computes T_Q
					T_Q_options |= GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2;
				} else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}
		} else {									// Multiplication
			// Compute FFT of s1 where necessary
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (s1 != d && !(options & GWMUL_PRESERVE_S1) && (FFT_state (s2R) != FULLY_FFTed || s2 == d || s3 == d))
					gwfft (gwdata, s1, s1);					// FFT both s1R and s1Q as a pair for possible future use
				else if (s2 != d && s3 != d)					// Can s1 be handled with a discardable forward FFT?
					s1Q = s1R, T_Q_options |= GWMUL_PRESERVE_S1;		// Delay the FFT for the mul that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}

			// Compute FFT of s2 where necessary
			if (FFT_state (s2R) != FULLY_FFTed) {					// If s2R is FFTed, so is s2Q
				if (s2 != d && !(options & GWMUL_PRESERVE_S2) && (FFT_state (s1R) != FULLY_FFTed || s1 == d || s3 == d))
					gwfft (gwdata, s2, s2);					// FFT both s2R and s2Q as a pair for possible future use
				else if (s1 != d && s3 != d && s1Q != s1R)			// Can mul discard FFT of s2?  Only one argument can be delayed.
					s2Q = s2R, T_Q_options |= GWMUL_PRESERVE_S2;		// Save a write by delaying the FFT to the mul that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s2R, s2Q);		// FFT into the currently unused s2Q buffer
			}
		}

		// T_Q = s1Q * s2Q +/- s3Q mod Q
		cmn_gwmulop4 (gwdata->negacyclic_gwdata, s1Q, s2Q, s3Q, T_Q, T_Q_options & ~GWMUL_STARTNEXTFFT, opcode);

		// T_R = s1R * s2R +/- s3R mod R
		cmn_gwmulop4 (gwdata->cyclic_gwdata, s1R, s2R, s3R, T_R, options | GWMUL_STARTNEXTFFT, opcode);

		// Reduction.  Multiply T_R by 2/N.
		MMGW_redc (gwdata, T_R, T_Q);
	}

/* If asm code cannot handle this feature (SSE2 and 32-bit), or the multiply carefully count is active, then emulate this feature. */

#ifdef X86_64
	else if (!(gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX)) || gwdata->careful_count > 0) {
#else
	else if (1) {
#endif

/* Make sure s3 is not FFTed */

		if (FFT_state (s3) != NOT_FFTed || s3 == d) {
			tmp3 = gwalloc (gwdata);
			gwunfft (gwdata, s3, tmp3);
			s3 = tmp3;
		}

/* Now emulate.  Be sure to do the optional mul-by-const after the FMA. */

		if ((options & GWMUL_MULBYCONST) && (options & GWMUL_ADDINCONST) && gwdata->asm_postaddin_value != 0.0) {
			// Multiply without the post addin value
			double saved_postaddin_value = gwdata->asm_postaddin_value;
			gwdata->asm_postaddin_value = 0.0;
			gwmul3 (gwdata, s1, s2, d, options & ~(GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
			gwdata->asm_postaddin_value = saved_postaddin_value;
			// Add/sub the FMA addin value
			if (opcode == 3) gwadd3o (gwdata, d, s3, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			else gwsub3o (gwdata, d, s3, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			// Mul by const and apply only the post addin value
			tmp1 = gwalloc (gwdata);
			dbltogw (gwdata, gwdata->mulbyconst, tmp1);
			double saved_addin_value = gwdata->asm_addin_value;
			gwdata->asm_addin_value = 0.0;
			gwmul3 (gwdata, d, tmp1, d, options & ~GWMUL_MULBYCONST);
			gwdata->asm_addin_value = saved_addin_value;
		} else if (options & GWMUL_MULBYCONST) {
			gwmul3 (gwdata, s1, s2, d, options & ~(GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
			if (opcode == 3) gwadd3o (gwdata, d, s3, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			else gwsub3o (gwdata, d, s3, d, GWADD_GUARANTEED_OK | GWADD_DELAY_NORMALIZE);
			gwsmallmul (gwdata, gwdata->mulbyconst, d);
		} else {
			gwmul3 (gwdata, s1, s2, d, options & ~GWMUL_STARTNEXTFFT);
			if (opcode == 3) gwadd3o (gwdata, d, s3, d, GWADD_FORCE_NORMALIZE);
			else gwsub3o (gwdata, d, s3, d, GWADD_FORCE_NORMALIZE);
		}
	}

/* Let assembly code do the work */

	else {
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;

/* Assume we will not be asking the assembly code to use two destinations */

		asm_data->DEST2ARG = 0;

/* Make sure s3 is FFTed */

		if (FFT_state (s3) != FULLY_FFTed && FFT_state (s3) != FFTed_FOR_FMA) {
			if (options & GWMUL_PRESERVE_S3) { tmp3 = gwalloc (gwdata); gwfft (gwdata, s3, tmp3); s3 = tmp3; }
			else gwfft (gwdata, s3, s3);
		}

/* The AVX/FMA3 code cannot do squaring with add/sub in assembly.  Convert to type-4 by FFTing source. */
/* This is not optimal.  Should some user have a strong need for optimizing this, we can revisit upgrading the assembly code. */

		if (s1 == s2 && !(gwdata->cpu_flags & CPU_AVX512F) && FFT_state (s1) != FULLY_FFTed) {
			if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
			else gwfft (gwdata, s1, s1);
		}

/* Handle squaring.  AVX-512 can handle two destination squaring, AVX and FMA3 cannot. */

		if (s1 == s2) {
			if (FFT_state (s1) == FULLY_FFTed) asm_data->ffttype = 4;
			else if (!(options & GWMUL_FFT_S12) && s3 != d) asm_data->ffttype = 2;
			else if (!(gwdata->cpu_flags & (CPU_AVX512F))) gwfft (gwdata, s1, s1), asm_data->ffttype = 4;
			else {	// Two destinations
				asm_data->ffttype = 2;
				asm_data->DEST2ARG = d;
			}
		}

/* Copy lots of code from raw_gwmul3.  The s2 argument must be FFTed prior to calling the assembly code.  However, the assembly */
/* code can FFT s1 and save the result while storing the result of the multiply in a different destination. */
/* Decide which of s1 and s2 is better suited to being s2. */

		else {
			gwnum	preferred_FFT_arg;	// Which source should be src1 in asm_mul which can be FFTed by asm_mul

/* If a source arg equals d then the other source args can be the type-3 forward FFT arg only if they use two destinations. */
/* Otherwise, the forward FFT of type-3 will write to d which corrupt the source arg that equals d. */

			bool s1_can_be_FFTed_and_discarded = (s2 != d && s3 != d);
			bool s2_can_be_FFTed_and_discarded = (s1 != d && s3 != d);

/* Pick which argument to FFT.  Prefer to FFT one that we can discard the FFT result (saves a write).  Only one of the first two sources can be preferred_FFT_arg. */

			// Type-4 FFT
			if (FFT_state (s1) == FULLY_FFTed && FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s1;
			// Next, prefer an arg that must be preserved (saves allocating a temporary and maybe a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && (options & GWMUL_PRESERVE_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && (options & GWMUL_PRESERVE_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Next, prefer an arg that we're allowed to leave in unFFTed state (may save a read-write)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_FFT_S1) && s1_can_be_FFTed_and_discarded) preferred_FFT_arg = s1;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_FFT_S2) && s2_can_be_FFTed_and_discarded) preferred_FFT_arg = s2;
			// Handle must FFT case using two destinations (may save a read)
			else if (FFT_state (s1) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S1)) preferred_FFT_arg = s1, asm_data->DEST2ARG = d;
			else if (FFT_state (s2) != FULLY_FFTed && !(options & GWMUL_PRESERVE_S2)) preferred_FFT_arg = s2, asm_data->DEST2ARG = d;
			// Rarely happens.  Preserve option forces us to use gwalloc, gwfft, and a type-4 multiply
			else if (FFT_state (s1) == FULLY_FFTed) preferred_FFT_arg = s1;
			else if (FFT_state (s2) == FULLY_FFTed) preferred_FFT_arg = s2;
			else ASSERTG (FALSE);

/* Pre-FFT all but the preferred FFT arg.  Even the preferred FFT arg may need to be FFTed. */

			if (preferred_FFT_arg != s1 && FFT_state (s1) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S1) { tmp1 = gwalloc (gwdata); gwfft (gwdata, s1, tmp1); s1 = tmp1; }
				else gwfft (gwdata, s1, s1);
			}
			if (preferred_FFT_arg != s2 && FFT_state (s2) != FULLY_FFTed) {
				if (options & GWMUL_PRESERVE_S2) { tmp2 = gwalloc (gwdata); gwfft (gwdata, s2, tmp2); s2 = tmp2; }
				else gwfft (gwdata, s2, s2);
			}

/* Determine the type of FFT multiply, rearrange sources so that preferred_FFT_arg is always the first arg passed to assembly code */

			if (FFT_state (preferred_FFT_arg) == FULLY_FFTed) asm_data->ffttype = 4;
			else {
				asm_data->ffttype = 3;
				if (preferred_FFT_arg == s2) {
					s2 = s1;
					s1 = preferred_FFT_arg;
				}
			}
		}

/* Pre-adjust counts for input that is not partially or fully FFTed */

		if (FFT_state (s1) == NOT_FFTed) gwdata->read_count += 1, gwdata->write_count += 1;

/* Call the assembly code */

		if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
		else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
		if (gwdata->FFT1_state == 2) asm_data->SRC3ARG = (void *) (intptr_t) 1;
		else asm_data->SRC3ARG = (void *) (intptr_t) 2;
		if (asm_data->DEST2ARG) {
			asm_data->mul4_opcode = 0x80 | opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s3 - (intptr_t) s1);
			asm_data->DEST2ARG = (void *) ((intptr_t) asm_data->DEST2ARG - (intptr_t) s1);
			asm_mul (gwdata, s1, s2, s1, options);
		} else {
			asm_data->mul4_opcode = opcode;
			asm_data->SRC2ARG = (void *) ((intptr_t) s3 - (intptr_t) d);
			asm_mul (gwdata, s1, s2, d, options);
		}

/* Post-adjust counts */

		gwdata->read_count += 4, gwdata->write_count += 2;
		if (s1 == s2 || s1 == s3) gwdata->read_count -= 1;
		if (s2 == s3) gwdata->read_count -= 1;
		if ((intptr_t) asm_data->SRC3ARG != 1) gwdata->read_count += 1;
		if (asm_data->DEST2ARG) gwdata->write_count += 1;

/* Emulate mod with 2 multiplies */

		if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
	}

/* Free temporary memory */			

	if (tmp1 != NULL) gwfree (gwdata, tmp1);
	if (tmp2 != NULL) gwfree (gwdata, tmp2);
	if (tmp3 != NULL) gwfree (gwdata, tmp3);
}

/************************************/
/*      User-visible routines       */
/************************************/

void gwfft (			/* Forward FFT */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d)		/* Destination (can overlap source) */
{
	struct gwasm_data *asm_data;

	ASSERTG (unnorms (s) >= 0.0f);

/* Handle easy case */

	if (FFT_state (s) == FULLY_FFTed) {
		if (s != d) gwcopy (gwdata, s, d);
		return;
	}

/* Undo a gwfft_for_fma call */

	if (FFT_state (s) == FFTed_FOR_FMA) {
		gwunfft (gwdata, s, d);
		s = d;
	}

/* For MMGW general mod, FFT mod R and FFT mod Q */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwunfft (gwdata, s, s);			// Make sure s is not partially FFTed as the FFT data is processed by cyclic and negacyclic gwdatas.
		gwfft (gwdata->negacyclic_gwdata, cyclic_gwnum (gwdata, s), negacyclic_gwnum (gwdata, d));
		gwfft (gwdata->cyclic_gwdata, cyclic_gwnum (gwdata, s), cyclic_gwnum (gwdata, d));
		return;
	}

/* If this is a zero-padded FFT and the source has been partially FFTed */
/* (by a prior POSTFFT setting) and the destination is different than the */
/* source, then we must also copy the 7 words around the halfway point from */
/* the source to the destination. */

	asm_data = (struct gwasm_data *) gwdata->asm_data;
	if (asm_data->ZERO_PADDED_FFT && FFT_state (s) == PARTIALLY_FFTed && s != d) {
		d[-5] = s[-5];
		d[-6] = s[-6];
		d[-7] = s[-7];
		d[-8] = s[-8];
		d[-9] = s[-9];
		d[-10] = s[-10];
		d[-11] = s[-11];
	}

/* Call the assembly code */

	asm_data->DESTARG = d;
	asm_data->DIST_TO_FFTSRCARG = (intptr_t) s - (intptr_t) d;
	asm_data->DIST_TO_MULSRCARG = 0;
	asm_data->ffttype = 1;
	gw_fft (gwdata, asm_data);

/* Update counts */

	gwdata->fft_count += 1;
	if (FFT_state (s) == PARTIALLY_FFTed) {
		gwdata->read_count += 1;
		gwdata->write_count += 1;
	} else {
		gwdata->read_count += 2;
		gwdata->write_count += 2;
	}

/* Copy the unnormalized add count, set has-been-FFTed flag */

	unnorms (d) = unnorms (s);
	FFT_state (d) = FULLY_FFTed;
}

/* Highly specialized FFT routine, only to be used on the third source argument to gwmuladd4 and gwmulsub4. */
/* Only use this if the number will be used many times as an input to gwmuladd4 or gwmulsub4. */

void gwfft_for_fma (		/* Forward FFT with post-processing for use in FMA */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d)		/* Destination (can overlap source) */
{
	int	i, j, doubles_per_vector, pass2_size;

	ASSERTG (unnorms (s) >= 0.0f);

/* Handle the easy case */

	if (FFT_state (s) == FFTed_FOR_FMA) {
		if (s != d) gwcopy (gwdata, s, d);
		return;
	}

/* For MMGW general mod, FFT mod R and FFT mod Q */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwunfft (gwdata, s, s);			// Make sure s is not partially or fully FFTed as the FFT data is processed by cyclic and negacyclic gwdatas.
		gwfft_for_fma (gwdata->negacyclic_gwdata, cyclic_gwnum (gwdata, s), negacyclic_gwnum (gwdata, d));
		gwfft_for_fma (gwdata->cyclic_gwdata, cyclic_gwnum (gwdata, s), cyclic_gwnum (gwdata, d));
		return;
	}

/* If we cannot do FMA in assembly code, put the number in an unffted state for faster FMA emulation. */

#ifdef X86_64
	if (!(gwdata->cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX))) {
#else
	if (1) {
#endif
		gwunfft (gwdata, s, d);
		return;
	}

/* Do a standard forward transform */

	gwfft (gwdata, s, d);

/* If the FMA routines are not passing FFT(1) to the assembly code, there is no need to do anything else */

	init_FFT1 (gwdata);
	if (gwdata->FFT1_state == 2) return;

/* Multiply the FFTed source by FFT(1).  For MMGW general mod there are two FFTs to multiply by FFT(1). */

	for (int which = 0; which == 0 || (gwdata->GENERAL_MMGW_MOD && which == 1); which++) {
		gwhandle *fft_gwdata;
		gwnum fft_d, GW_FFT1;

		if (!gwdata->GENERAL_MMGW_MOD) {
			fft_gwdata = gwdata;
			fft_d = d;
			GW_FFT1 = gwdata->GW_FFT1;
		} else if (which == 0) {
			fft_gwdata = gwdata->cyclic_gwdata;
			fft_d = cyclic_gwnum (gwdata, d);
			GW_FFT1 = cyclic_gwnum (gwdata, gwdata->GW_FFT1);
		} else {
			fft_gwdata = gwdata->negacyclic_gwdata;
			fft_d = negacyclic_gwnum (gwdata, d);
			GW_FFT1 = negacyclic_gwnum (gwdata, gwdata->GW_FFT1);
		}

		if (fft_gwdata->cpu_flags & CPU_AVX512F) {
			doubles_per_vector = 8;
			pass2_size = fft_gwdata->PASS2_SIZE ? fft_gwdata->PASS2_SIZE * 2 : fft_gwdata->FFTLEN;
			for (i = 0; i < (int) fft_gwdata->FFTLEN; i += doubles_per_vector * 2) {
			    for (j = 0; j < doubles_per_vector; j++) {
				int	k = i + j + (i / pass2_size) * (fft_gwdata->PASS2GAPSIZE / sizeof (double)) + (i / 512) * (fft_gwdata->FOURKBGAPSIZE / sizeof (double));
				double	real1 = fft_d[k];
				double	imag1 = fft_d[k + doubles_per_vector];
				double	real2 = GW_FFT1[k];
				double	imag2 = GW_FFT1[k + doubles_per_vector];
				if (k == 0 && !fft_gwdata->NEGACYCLIC_FFT) {
					fft_d[0] = real1 * real2;
					fft_d[doubles_per_vector] = imag1 * imag2;
				} else {
					fft_d[k] = real1 * real2 - imag1 * imag2;
					fft_d[k + doubles_per_vector] = real1 * imag2 + imag1 * real2;
				}
			    }
			}
		} else {
			doubles_per_vector = 4;
			for (i = 0; i < (int) (gwnum_datasize (fft_gwdata) / sizeof (double)); i += doubles_per_vector * 2) {
			    for (j = 0; j < doubles_per_vector; j++) {
				double	real1 = fft_d[i + j];
				double	imag1 = fft_d[i + j + doubles_per_vector];
				double	real2 = GW_FFT1[i + j];
				double	imag2 = GW_FFT1[i + j + doubles_per_vector];
				if (i == 0 && j == 0 && !fft_gwdata->NEGACYCLIC_FFT) {
					fft_d[0] = real1 * real2;
					fft_d[doubles_per_vector] = imag1 * imag2;
				} else {
					fft_d[i + j] = real1 * real2 - imag1 * imag2;
					fft_d[i + j + doubles_per_vector] = real1 * imag2 + imag1 * real2;
				}
			    }
			}
		}

/* If this is a zero-padded FFT then we must also work on the 7 words around the halfway point */

		if (fft_gwdata->ZERO_PADDED_FFT) {
			double	wm3, wm2, wm1, w0, w1, w2, w3;
			double	xm3, xm2, xm1, x0, x1, x2, x3;
			wm3 = d[-11];			// Load copy of add-in FFT word at halfway - 3
			wm2 = d[-10];			// Load copy of add-in FFT word at halfway - 2
			wm1 = d[-9];			// Load copy of add-in FFT word at halfway - 1
			xm3 = gwdata->GW_FFT1[-11];	// Load copy of FFT(1) word at halfway - 3
			xm2 = gwdata->GW_FFT1[-10];	// Load copy of FFT(1) word at halfway - 2
			xm1 = gwdata->GW_FFT1[-9];	// Load copy of FFT(1) word at halfway - 1
			w0 = d[-8];			// Load copy of add-in FFT word at halfway + 0
			w1 = d[-7];			// Load copy of add-in FFT word at halfway + 1
			w2 = d[-6];			// Load copy of add-in FFT word at halfway + 2
			w3 = d[-5];			// Load copy of add-in FFT word at halfway + 3
			x0 = gwdata->GW_FFT1[-8];	// Load copy of add-in FFT word at halfway + 0
			x1 = gwdata->GW_FFT1[-7];	// Load copy of add-in FFT word at halfway + 1
			x2 = gwdata->GW_FFT1[-6];	// Load copy of add-in FFT word at halfway + 2
			x3 = gwdata->GW_FFT1[-5];	// Load copy of add-in FFT word at halfway + 3
			d[-11] = wm3 * x3 + wm2 * x2 + wm1 * x1 +		// Addin0 = word-3 * word3 + word-2 * word2 + word-1 * word1 +
				 w0 * x0 + w1 * xm1 + w2 * xm2 + w3 * xm3;	//	    word0 * word0 + word1 * word-1 + word2 * word-2 + word3 * word-3
			d[-10] = wm2 * x3 + wm1 * x2 + w0 * x1 +		// Addin1 = word-2 * word3 + word-1 * word2 + word0 * word1 +
				 w1 * x0 + w2 * xm1 + w3 * xm2;			//	    word1 * word0 + word2 * word-1 + word3 * word-2
			d[-9] = wm1 * x3 + w0 * x2 + w1 * x1 +			// Addin2 = word-1 * word3 + word0 * word2 + word1 * word1
				w2 * x0 + w3 * xm1;				//	    word2 * word0 + word3 * word-1
			d[-8] = w0 * x3 + w1 * x2 + w2 * x1 + w3 * x0;		// Addin3 = word0 * word3 + word1 * word2 + word2 * word1 + word3 * word0
			d[-7] = w1 * x3 + w2 * x2 + w3 * x1;			// Addin4 = word1 * word3 + word2 * word2 + word3 * word1
			d[-6] = w2 * x3 + w3 * x2;				// Addin5 = word2 * word3 + word3 * word2
			d[-5] = w3 * x3;					// Addin6 = word3 * word3
		}

/* Set has-been-FFTed flag, update counts */

		FFT_state (fft_d) = FFTed_FOR_FMA;
	}
	gwdata->read_count += 1;
	gwdata->write_count += 1;
}

/* Computes d = s1 * s2 */
/* A temporary is required if both s1 and s2 must be preserved and neither has already been FFTed and d != s1 and d != s2 */
/* Options exist for preserving source args, replacing source args with their FFTs, or if neither gwmul3 is free to overwrite source arg. */
/* S1 and s2 can be the same address.  Destination can be the same as s1 or s2. */

void gwmul3 (			/* Multiply two gwnums */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	struct gwasm_data *asm_data;

	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	// Make sure not too many unnormalized adds prior to square or multiply
	ASSERTG ((s1 == s2 && square_safe (gwdata, unnorms (s1))) ||
		 (s1 != s2 && mul_safe (gwdata, unnorms (s1), unnorms (s2))));

/* Clear flags that are impossible when a source is the same as the destination */

	if (s1 == d) options &= ~(GWMUL_FFT_S1 | GWMUL_PRESERVE_S1);
	if (s2 == d) options &= ~(GWMUL_FFT_S2 | GWMUL_PRESERVE_S2);

/* Check the careful count */

	if (gwdata->careful_count > 0) {
		gwdata->careful_count--;
		gwmul3_carefully (gwdata, s1, s2, d, options);
		return;
	}

/* See if GWMUL_ADDINCONST must be emulated */

	if ((options & GWMUL_ADDINCONST) && (gwdata->emulate_addin_value != 0 || gwdata->emulate_postaddin_value != 0)) {
		gwmul3 (gwdata, s1, s2, d, options & ~(GWMUL_MULBYCONST | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT));
		init_GW_ADDIN (gwdata);
		if (gwdata->GW_ADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_ADDIN, d, GWADD_FORCE_NORMALIZE);	//GW Use GWADD_GUARANTEED_OK if mulbyconst or postaddin
		if (options & GWMUL_MULBYCONST) gwsmallmul (gwdata, gwdata->mulbyconst, d);
		if (gwdata->GW_POSTADDIN != NULL) gwadd3o (gwdata, d, gwdata->GW_POSTADDIN, d, GWADD_FORCE_NORMALIZE);
		return;
	}

/* Handle general mod with Montgomery-McLaughlin-Gallot-Woltman multiplication and reduction */

	if (gwdata->GENERAL_MMGW_MOD) {
		bool mulmulsub5able = (!(options & GWMUL_MULBYCONST) && !(options & GWMUL_ADDINCONST));
		int T_Q_options = options;

		// Get pointers to the six gwnums
		gwnum s1R = cyclic_gwnum (gwdata, s1);
		gwnum s1Q = negacyclic_gwnum (gwdata, s1);
		gwnum s2R = cyclic_gwnum (gwdata, s2);
		gwnum s2Q = negacyclic_gwnum (gwdata, s2);
		gwnum T_R = cyclic_gwnum (gwdata, d);
		gwnum T_Q = negacyclic_gwnum (gwdata, d);

		// Apply request to FFT input arguments.  FUTURE: It might be possible to use two-destination FFTs in some cases.
		if (options & GWMUL_FFT_S1) gwfft (gwdata, s1, s1);
		if (options & GWMUL_FFT_S2) gwfft (gwdata, s2, s2);

		// Different code for squaring and multiplication
		if (s1 == s2) {									// Squaring
			mulmulsub5able = mulmulsub5able && mulsquareadd_safe (gwdata->negacyclic_gwdata, 0, 0, unnorms (s1), unnorms (s1));

			// Compute s1Q = negacyclic FFT of s1 where necessary
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (!mulmulsub5able) {
					s1Q = s2Q = s1R;					// Save a write by delaying the FFT to the gwmul3 that computes T_Q
					T_Q_options |= GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2;
				} else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}
		} else {									// Multiplication
			mulmulsub5able = mulmulsub5able && mulmuladd_safe (gwdata->negacyclic_gwdata, 0, 0, unnorms (s1), unnorms (s2));

			// Compute s1Q = negacyclic FFT of s1 where necessary
			if (FFT_state (s1R) != FULLY_FFTed) {					// If s1R is FFTed, so is s1Q
				if (s1 != d && !(options & GWMUL_PRESERVE_S1) && (FFT_state (s2R) != FULLY_FFTed || s2 == d))
					gwfft (gwdata, s1, s1);					// FFT both s1R and s1Q as a pair for possible future use
				else if (!mulmulsub5able && s2 != d)				// Can raw_gwmul3 save a write with a discardable forward FFT?
					s1Q = s1R, T_Q_options |= GWMUL_PRESERVE_S1;		// Save a write by delaying the FFT to the gwmul3 that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s1R, s1Q);		// FFT into the currently unused s1Q buffer
			}

			// Compute s2Q = negacyclic FFT of s2 where necessary
			if (FFT_state (s2R) != FULLY_FFTed) {					// If s2R is FFTed, so is s2Q
				if (s2 != d && !(options & GWMUL_PRESERVE_S2) && (FFT_state (s1R) != FULLY_FFTed || s1 == d))
					gwfft (gwdata, s2, s2);					// FFT both s2R and s2Q as a pair for possible future use
				else if (!mulmulsub5able && s1 != d && s1Q != s1R)		// Can raw_gwmul3 discard FFT of s2?  Only one argument can be delayed.
					s2Q = s2R, T_Q_options |= GWMUL_PRESERVE_S2;		// Save a write by delaying the FFT to the gwmul3 that computes T_Q
				else
					gwfft (gwdata->negacyclic_gwdata, s2R, s2Q);		// FFT into the currently unused s2Q buffer
			}
		}

		// Compute T_Q = s1Q * s2Q mod Q.
		if (!mulmulsub5able) gwmul3 (gwdata->negacyclic_gwdata, s1Q, s2Q, T_Q, T_Q_options & ~GWMUL_STARTNEXTFFT);

		// T_R = s1R * s2R mod R
		gwmul3 (gwdata->cyclic_gwdata, s1R, s2R, T_R, options | GWMUL_STARTNEXTFFT);

		// Reduction.  Multiply T_R by 2/N.
		if (!mulmulsub5able) MMGW_redc (gwdata, T_R, T_Q);
		else MMGW_redc5 (gwdata, T_R, s1Q, s2Q);

		return;
	}

/* Call the assembly code */

	asm_data = (struct gwasm_data *) gwdata->asm_data;
	if (options & GWMUL_MULBYCONST) asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + 2 + gwdata->ERROR_CHECKING];
	else asm_data->NORMRTN = gwdata->GWPROCPTRS[norm_routines + gwdata->ERROR_CHECKING];
	raw_gwmul3 (gwdata, s1, s2, d, options);

/* Emulate mod with 2 multiplies case */

	if (gwdata->GENERAL_MOD) emulate_mod (gwdata, d);
}

/* Computes d = s1 * s2.  Handles non-random inputs which might otherwise lead to a large round-off error. */

void gwmul3_carefully (		/* Multiply two gwnums very carefully */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	int	saved_careful_count;

	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	// Make sure not too many unnormalized adds prior to square or multiply
	ASSERTG ((s1 == s2 && square_safe (gwdata, unnorms (s1))) ||
		 (s1 != s2 && mul_safe (gwdata, unnorms (s1), unnorms (s2))));

/* Clear flags that are impossible when a source is the same as the destination */

	if (s1 == d) options &= ~(GWMUL_FFT_S1 | GWMUL_PRESERVE_S1);
	if (s2 == d) options &= ~(GWMUL_FFT_S2 | GWMUL_PRESERVE_S2);

/* It is dangerous to FFT non-random data.  This routine was called because the source data may be non-random. */

	options &= ~GWMUL_FFT_S12;

/* Save and clear the carefully count (so that we don't recursively call this routine) */

	saved_careful_count = gwdata->careful_count;
	gwdata->careful_count = 0;

/* Generate a random number, if we have't already done so */

	// If this is a cloned gwdata, see if we can save some memory by using the parent GW_RANDOM
	if (gwdata->clone_of != NULL && gwdata->GW_RANDOM != gwdata->clone_of->GW_RANDOM && gwdata->clone_of->GW_RANDOM != NULL) {
		gwfree (gwdata, gwdata->GW_RANDOM); gwdata->GW_RANDOM = gwdata->clone_of->GW_RANDOM;
		gwfree (gwdata, gwdata->GW_RANDOM_SQUARED); gwdata->GW_RANDOM_SQUARED = gwdata->clone_of->GW_RANDOM_SQUARED;
		gwfree (gwdata, gwdata->GW_RANDOM_FFT); gwdata->GW_RANDOM_FFT = gwdata->clone_of->GW_RANDOM_FFT;
	}
	// Otherwise, allocate locally
	else if (gwdata->GW_RANDOM == NULL) {
		gwnum random = gwalloc_internal (gwdata);
		gw_fixed_random_number (gwdata, random);
		gwdata->GW_RANDOM = random;				// For gwclone's sake, do not set GW_RANDOM until random number is fully initialized
		ASSERTG (gwdata->GW_RANDOM_SQUARED == NULL && gwdata->GW_RANDOM_FFT == NULL);
	}

/* Handle a squaring operation using two multiplies and several adds. */
/* Make sure we do not do addquick when computing s+random.  If we do not do this, then the non-randomness of s can swamp */
/* the randomness of s+random.  An example is the first PRP iterations of 2*3^599983-1 -- s is all positive values and gwadd3 */
/* thinks there are enough extra bits to do an add quick.  This generates temps with nearly all positive values -- very bad. */

	if (s1 == s2) {
		gwnum tmp = gwalloc (gwdata);
		if (FFT_state (s1) == NOT_FFTed);							/* Make sure input data is not FFTed */
		else if (options & (GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2)) gwunfft (gwdata, s1, tmp), s1 = tmp;
		else gwunfft (gwdata, s1, s1);

		gwaddsub4o (gwdata, s1, gwdata->GW_RANDOM, tmp, d, GWADD_FORCE_NORMALIZE);		/* Compute s1+random and s1-random */

		/* If not already cached, square the random number */
		if (gwdata->GW_RANDOM_SQUARED == NULL) {
			// If this is a cloned gwdata, see if we can save some memory by using the parent GW_RANDOM_SQUARED
			if (gwdata->clone_of != NULL && gwdata->GW_RANDOM == gwdata->clone_of->GW_RANDOM && gwdata->clone_of->GW_RANDOM_SQUARED != NULL) {
				gwdata->GW_RANDOM_SQUARED = gwdata->clone_of->GW_RANDOM_SQUARED;
			}
			// Otherwise, allocate locally
			else {
				gwnum random_squared = gwalloc_internal (gwdata);
				gwmul3 (gwdata, gwdata->GW_RANDOM, gwdata->GW_RANDOM, random_squared, GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2 | GWMUL_STARTNEXTFFT);
				gwfft_for_fma (gwdata, random_squared, random_squared);
				gwdata->GW_RANDOM_SQUARED = random_squared;	// For gwclone's sake, do not set GW_RANDOM_SQUARED until it is fully initialized
			}
		}

		/* Generate desired result, d = ((s1+r)*(s1-r) + r^2 + addin) * mulbyconst + postaddin */
		gwmuladd4 (gwdata, tmp, d, gwdata->GW_RANDOM_SQUARED, d, options & (GWMUL_ADDINCONST | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));

		gwfree (gwdata, tmp);
	}

/* Handle a multiply operation.  A more complex method is required using two multiplies and several adds. */
/* ((s1+r)*(s2-r)+addin)*mulbyconst+postaddin = (s1s2 + addin) * mulbyconst + postaddin - (r * (s1 + r - s2)) * mulbyconst */

	else {
		gwnum	tmp1, tmp2, tmp3;

		tmp1 = gwalloc (gwdata);
		if (FFT_state (s1) == NOT_FFTed);					/* Make sure s1 input data is not FFTed */
		else if (options & GWMUL_PRESERVE_S1) gwunfft (gwdata, s1, tmp1), s1 = tmp1;
		else gwunfft (gwdata, s1, s1);

		gwadd3o (gwdata, s1, gwdata->GW_RANDOM, tmp1, GWADD_FORCE_NORMALIZE);	/* Compute s1+random */

		tmp2 = NULL;
		if (FFT_state (s2) == NOT_FFTed);					/* Make sure s2 input data is not FFTed */
		else if (options & GWMUL_PRESERVE_S2) tmp2 = gwalloc (gwdata), gwunfft (gwdata, s2, tmp2), s2 = tmp2;
		else gwunfft (gwdata, s2, s2);

		tmp3 = gwalloc (gwdata);
		gwsub3o (gwdata, s2, gwdata->GW_RANDOM, tmp3, GWADD_FORCE_NORMALIZE);	/* Compute s2-random */
		gwsub3o (gwdata, tmp1, s2, d, GWADD_FORCE_NORMALIZE);			/* Compute s1+random-s2 */
		gwfree (gwdata, tmp2);

		/* If not already cached, FFT the random number */
		if (gwdata->GW_RANDOM_FFT == NULL) {
			// If this is a cloned gwdata, see if we can save some memory by using the parent GW_RANDOM_FFT
			if (gwdata->clone_of != NULL && gwdata->GW_RANDOM == gwdata->clone_of->GW_RANDOM && gwdata->clone_of->GW_RANDOM_FFT != NULL) {
				gwdata->GW_RANDOM_FFT = gwdata->clone_of->GW_RANDOM_FFT;
			}
			// Otherwise, allocate locally
			else {
				gwnum random_fft = gwalloc_internal (gwdata);
				gwfft (gwdata, gwdata->GW_RANDOM, random_fft);
				gwdata->GW_RANDOM_FFT = random_fft;		// For gwclone's sake, do not set GW_RANDOM_FFT until it is fully initialized
			}
		}
		
		/* Generate desired result, d = ((s1+r)*(s2-r) + r*(s1+r-s2) + addin) * mulbyconst + postaddin */
		if (!gwdata->paranoid_mul_careful || mulsquareadd_safe (gwdata, 0, 0, 0, 0)) {
			gwmulmuladd5 (gwdata, tmp1, tmp3, gwdata->GW_RANDOM_FFT, d, d, options & (GWMUL_ADDINCONST | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
		} else {
			// Paranoid code does not use gwmulmuladd5 in case the value in s1 and s2 are identical which means we are computing r^2 which
			// may generate a larger roundoff error.
			gwmul3 (gwdata, gwdata->GW_RANDOM_FFT, d, d, GWMUL_STARTNEXTFFT);
			gwmuladd4 (gwdata, tmp1, tmp3, d, d, options & (GWMUL_ADDINCONST | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT));
		}

		gwfree (gwdata, tmp1);
		gwfree (gwdata, tmp3);
	}

/* Restore from saved values */

	gwdata->careful_count = saved_careful_count;
}

/* Special multiply routine that adds two numbers and multiplies by a third.  This is faster if s1 and s2 have already been FFTed. */
/* This is particularly handy in stage 2 of P-1 and ECM. */ 

void gwaddmul4 (		/* Calculate (s1+s2)*s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (addmul_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3)));
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA && FFT_state (s3) != FFTed_FOR_FMA);

/* Let common code do the work */

	cmn_gwopmul4 (gwdata, s1, s2, s3, d, options, 1);	// 1 = add
}

/* Special multiply routine that subtracts two numbers and multiplies by a third.  This is faster if s1 and s2 have already been FFTed. */
/* This is particularly handy in stage 2 of P-1 and ECM. */ 

void gwsubmul4 (		/* Calculate (s1-s2)*s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (addmul_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3)));
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA && FFT_state (s3) != FFTed_FOR_FMA);

/* Let common code do the work */

	cmn_gwopmul4 (gwdata, s1, s2, s3, d, options, 2);	// 2 = subtract
}

/* Special multiply routine that multiplies two numbers and adds a third.  This is faster if s3 is already FFTed. */
/* There is almost no limit on the unnormalized adds done on s3. */

void gwmuladd4 (		/* Calculate (s1*s2)+s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	// Make sure not too many unnormalized adds prior to square or multiply
	ASSERTG ((s1 == s2 && squareadd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3))) ||
		 (s1 != s2 && muladd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3))));

/* Let common code do the work */

	cmn_gwmulop4 (gwdata, s1, s2, s3, d, options, 3);	// 3 = muladd
}

/* Special multiply routine that multiplies two numbers and subtracts a third.  This is faster if s3 is already FFTed. */
/* There is almost no limit on the unnormalized adds done on s3. */

void gwmulsub4 (		/* Calculate (s1*s2)-s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	// Make sure not too many unnormalized adds prior to square or multiply
	ASSERTG ((s1 == s2 && squareadd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3))) ||
		 (s1 != s2 && muladd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3))));

/* Let common code do the work */

	cmn_gwmulop4 (gwdata, s1, s2, s3, d, options, 4);	// 4 = mulsub
}

/* Special multiply routine that multiplies two numbers and adds the product of a third and fourth.  This is faster if s3 and s4 are already FFTed. */

void gwmulmuladd5 (		/* Calculate (s1*s2)+(s3*s4) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	s4,		/* Fourth source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (unnorms (s4) >= 0.0f);
	ASSERTG (s1 != s2 || s1 != s3 || s1 != s4);  // All sources equal is a silly way to calc 2*s1^2
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	ASSERTG (FFT_state (s3) != FFTed_FOR_FMA && FFT_state (s4) != FFTed_FOR_FMA);
	ASSERTG (mulmuladd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3), unnorms (s4)));

/* Let common code do the work */

	cmn_gwmulmulop5 (gwdata, s1, s2, s3, s4, d, options, 3);	// 3 = muladd
}

/* Special multiply routine that multiplies two numbers and subtracts the product of a third and fourth.  This is faster if s3 and s4 are already FFTed. */

void gwmulmulsub5 (		/* Calculate (s1*s2)-(s3*s4) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	s4,		/* Fourth source */
	gwnum	d,		/* Destination */
	int	options)	/* See gwnum.h */
{
	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (unnorms (s3) >= 0.0f);
	ASSERTG (unnorms (s4) >= 0.0f);
	ASSERTG (mulmuladd_safe (gwdata, unnorms (s1), unnorms (s2), unnorms (s3), unnorms (s4)));
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);
	ASSERTG (FFT_state (s3) != FFTed_FOR_FMA && FFT_state (s4) != FFTed_FOR_FMA);

/* Let common code do the work */

	cmn_gwmulmulop5 (gwdata, s1, s2, s3, s4, d, options, 4);	// 4 = mulsub
}

/* Generate random FFT data.  We used to use the C runtime library. */
/* However, when a caller discovered a bug in gwsquare_carefully it was */
/* very difficult to track down because the bug was not reproducible. */
/* We could make bugs reproducible by calling srand with a fixed value, */
/* but it is bad form for a library to do this.  Thus, we found a */
/* public domain random number generator to use. */

void gw_random_number_internal (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x,
	struct mt_state *rand_info)
{
	stackgiant(g,4);

/* Generate a random number.  Start with a 128-bit random odd number and square it several times. */

	g->sign = 4;
	g->n[0] = (uint32_t) genrand_int32 (rand_info) | 1;
	g->n[1] = (uint32_t) genrand_int32 (rand_info);
	g->n[2] = (uint32_t) genrand_int32 (rand_info);
	g->n[3] = (uint32_t) genrand_int32 (rand_info) | 0x80000000;
	if (gwdata->GW_MODULUS != NULL) {
		modg (gwdata->GW_MODULUS, g);
		if (isZero (g) || isone (g)) itog (3, g);
	}
	gianttogw (gwdata, g, x);
	// Square until number exceeds MODULUS. Do 4 extra squarings "for good measure".
	for (unsigned int bitlen = 128 >> 4; bitlen < (unsigned int) gwdata->bit_length; bitlen <<= 1) {
		char	saved_error_checking = gwdata->ERROR_CHECKING;
		int	saved_careful_count = gwdata->careful_count;
		gwerror_checking (gwdata, 0);			// Turn off error checking
		gwdata->careful_count = 0;			// Disable square carefully count
		gwsquare2 (gwdata, x, x, 0);
		gwerror_checking (gwdata, saved_error_checking);
		gwdata->careful_count = saved_careful_count;
	}
}

/* Generate random numbers using a random seed */

void gw_random_number (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x)
{
static	int	genrand_initialized = FALSE;
static	struct mt_state rand_info;

/* Init the random generator to a random starting point */

	if (!genrand_initialized) {
		init_genrand (&rand_info, (unsigned long) time (NULL));
		genrand_initialized = TRUE;
	}

/* Generate a random number */

	gw_random_number_internal (gwdata, x, &rand_info);
}

/* Generate a "fixed" random number using a fixed seed */

void gw_fixed_random_number (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x)
{
	struct mt_state rand_info;

/* Init the random generator to a reproducible state for gwsquare_carefully and gwmul_carefully */

	init_genrand (&rand_info, 5489);

/* Generate a random number */

	gw_random_number_internal (gwdata, x, &rand_info);
}

/* The FFT selection code assumes FFT data will essentially be random data2 yielding pretty well understood maximum */
/* round off errors.  When working  with some numbers, especially at the start of a PRP exponentiation, the FFT data */
/* is decidedly not random, leading to much larger than expected roundoff errors.  In my own PRP code, I call */
/* gwsquare_carefully for the first 30 iterations.  To make this easier (and code more readable) you can call this */
/* routine and the next n gwsquare or gwmul3 calls will be replaced by gwmul3_carefully calls.  If you pass an n of -1, */
/* the gwnum code will use a default value for n that should be suitable for getting a PRP exponentiation into a */
/* "random data state".  This routine can be called before gwsetup is called. */

void gwset_carefully_count (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	int	n)		/* Number of gwsquare calls to do carefully. */
				/* If n is -1, a default value is used */
{

/* Set the default count to enough iterations for a PRP exponentiation to generate a number larger than the modulus. */
/* Then do an extra dozen iterations to hopefully scramble the data into a nice random looking pattern. */

	if (n == -1 && gwdata->FFTLEN) n = (int) ceil (log2 (gwdata->bit_length)) + 12;

/* Now remember the count */

	gwdata->careful_count = n;
}

/********************************************************************/
/* Helper routines for the multithreaded add/sub/smallmul/etc. code */
/********************************************************************/

/* Return a pointer to the big/lit flags within the first pass 1 block's variable data */

static __inline void *biglit_data_ptr (
	gwhandle *gwdata,
	int	i)
{
	char	*p;

	// This code only works on AVX-512 FFTs.  Earlier FFT implementations did not store big/lit flags in the variable data
	ASSERTG (gwdata->cpu_flags & CPU_AVX512F && gwdata->PASS2_SIZE);

	// Calculate address of biglit data in the first pass 1 block's variable data.
	p = (char *) gwdata->pass1_var_data + gwdata->biglit_data_offset;

	// Point to the correct set of big/lit flags
	if (gwdata->ZERO_PADDED_FFT) return (p + (i * gwdata->PASS1_CACHE_LINES * 2 / 8 / 2));
	else return (p + (i * gwdata->PASS1_CACHE_LINES * 2 / 8));
}

/* Structure to share add/sub/addsub/smallmul data amongst all the compute threads */

struct multithread_op_data {
	gwnum	s1;			/* Source #1 */
	gwnum	s2;			/* Source #2 */
	gwnum	d1;			/* Destination #1 */
	gwnum	d2;			/* Destination #2 (if operation is addsub) */
	void	(*asm_proc)(void*);	/* Pointer to assembly routine that will perform the operation */
	int	is_quick;		/* TRUE if this is a "quick" operation that does not require carry propagation */
	int	num_blks;		/* Number of "blocks" to process */
	void	*d1_carries;		/* Carries area for destination #1 calculations */
	void	*d2_carries;		/* Carries area for destination #2 calculations */
};

/* Perform a multithreaded add/sub/addsub/smallmul operation */

void multithread_op (
	gwhandle *gwdata,		/* Handle initialized by gwsetup */
	gwnum	s1,			/* Source #1 */
	gwnum	s2,			/* Source #2 */
	gwnum	d1,			/* Destination #1 */
	gwnum	d2,			/* Destination #2 (if operation is addsub) */
	void	(*asm_proc)(void*),	/* Pointer to assembly routine that will perform the operation */
	int	is_quick)		/* TRUE if this is a "quick" operation that does not require carry propagation */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	struct multithread_op_data data;

// Common data structure initialization for both AVX and AVX-512

	data.s1 = s1;
	data.s2 = s2;
	data.d1 = d1;
	data.d2 = d2;
	data.asm_proc = asm_proc;
	data.is_quick = is_quick;

/* Handle gwcopy for all architectures */
//BUG - is there a better alternative to memcpy?  Like using SSE/AVX/AVX-512 loads and stores.
//BUG - is there any particular advantage to aligning moves on 128, 256, or 4096 byte boundary?

	if (asm_proc == NULL) {
		int	size, movesize;

		size = 7 * sizeof (uint32_t) + gwnum_datasize (gwdata);
		data.s1 = (gwnum) ((char *) &(((uint32_t *) data.s1)[-7]));
		data.d1 = (gwnum) ((char *) &(((uint32_t *) data.d1)[-7]));

		// Move any bytes before a 128-byte boundary
		if ((intptr_t) data.s1 & 127) {
			movesize = 128 - (int) ((intptr_t) data.s1 & 127);
			memcpy (data.d1, data.s1, movesize);
			size -= movesize;
			data.s1 = (gwnum) ((char *) data.s1 + movesize);
			data.d1 = (gwnum) ((char *) data.d1 + movesize);
		}

		// Calculate number of 4KB blocks
		data.num_blks = size >> 12;

		// Move any bytes after last 4KB block
		if (size & 4095) {
			memcpy ((char *) data.d1 + data.num_blks * 4096, (char *) data.s1 + data.num_blks * 4096, size & 4095);
		}
	}

/* Initialize the structure to share necessary info amongst the AVX-512 compute threads */

	else if (gwdata->cpu_flags & CPU_AVX512F) {
		data.num_blks = asm_data->addcount1;
		data.d1_carries = asm_data->carries;
		// BUG/OPT - use scratch area rather than aligned_malloc?  one-pass FFTs are an issue.  We'd need to make
		// sure scratch area is big enough and pre-allocate area for one-pass FFTs
		if (d2 != NULL && !is_quick) data.d2_carries = aligned_malloc (data.num_blks * 16 * sizeof (double), 128);
	}

/* Initialize the structure to share necessary info amongst the AVX compute threads */

	else {
		data.num_blks = asm_data->addcount1;
		data.d1_carries = asm_data->carries;
		// BUG/OPT - use scratch area rather than aligned_malloc?  We'd need to make sure scratch area is big enough
		if (d2 != NULL && !is_quick) data.d2_carries = aligned_malloc (data.num_blks * 8 * sizeof (double), 128);
	}

/* Wake up the auxiliary threads */

	gwdata->pass1_state = PASS1_STATE_MULTITHREAD_OP;
	gwdata->multithread_op_data = &data;
	atomic_set (gwdata->next_block, 0);
	if (gwdata->num_threads > 1) signal_auxiliary_threads (gwdata);

/* Call subroutine to work on blocks just like the auxiliary threads */

	do_multithread_op_work (gwdata, asm_data);

/* Wait for auxiliary threads to finish */

	if (gwdata->num_threads > 1) wait_on_auxiliary_threads (gwdata);

/* If no carry propagation is required then we're done */

	if (is_quick) return;

/* Set flags used in processing carries */

	asm_data->zero_fft = 0;
	asm_data->const_fft = 0;
	asm_data->add_sub_smallmul_op = 1;

/* Do the AVX-512 carry propagation */

	if (gwdata->cpu_flags & CPU_AVX512F) {

/* Do the final carry propagations for the dest #1 */

		asm_data->DESTARG = d1;		// Start of the FFT data
		asm_data->data_addr = d1;	// FFT addr to apply carries
		asm_data->norm_ptr1 = biglit_data_ptr (gwdata, 0);
		asm_data->this_block = 0;
		gwz3_apply_carries (asm_data);

/* Do the final carry propagations for the dest #2 */

		if (d2 != NULL) {
			asm_data->carries = data.d2_carries;
			asm_data->DESTARG = d2;		// Start of the FFT data
			asm_data->data_addr = d2;	// FFT addr to apply carries
			asm_data->norm_ptr1 = biglit_data_ptr (gwdata, 0);
			asm_data->this_block = 0;
			gwz3_apply_carries (asm_data);
			asm_data->carries = data.d1_carries;
			aligned_free (data.d2_carries);
		}
	}

/* Do the AVX carry propagation */

	else {

/* Do the final carry propagations for the dest #1 */

		asm_data->DESTARG = d1;		// Start of the FFT data
 		asm_data->data_addr = d1;	// FFT addr to apply carries
		asm_data->norm_ptr1 = asm_data->norm_biglit_array;
		asm_data->this_block = 0;
		gwy3_apply_carries (asm_data);

/* Do the final carry propagations for the dest #2 */

		if (d2 != NULL) {
			asm_data->carries = data.d2_carries;
			asm_data->DESTARG = d2;		// Start of the FFT data
			asm_data->data_addr = d2;	// FFT addr to apply carries
			asm_data->norm_ptr1 = asm_data->norm_biglit_array;
			asm_data->this_block = 0;
			gwy3_apply_carries (asm_data);
			asm_data->carries = data.d1_carries;
			aligned_free (data.d2_carries);
		}
	}
}

/* Routine for the main thread and auxiliary threads to do add/sub/addsub/smallmul work */

void do_multithread_op_work (
	gwhandle *gwdata,		/* Handle initialized by gwsetup */
	struct gwasm_data *asm_data)	/* This thread's asm_data */
{
	struct multithread_op_data *data = (struct multithread_op_data *) gwdata->multithread_op_data;

/* Loop processing gwcopy blocks (4KB) */

	if (data->asm_proc == NULL) {
		for ( ; ; ) {

/* Get next block to process.  Break out of loop when there are no more blocks to process. */

			int i = (int) atomic_fetch_incr (gwdata->next_block);
			if (i >= data->num_blks) break;

/* Move a 4KB block */

			memcpy ((char *) data->d1 + i * 4096, (char *) data->s1 + i * 4096, 4096);
		}
	}

/* Loop processing AVX-512 blocks */

	else if (gwdata->cpu_flags & CPU_AVX512F) {
		for ( ; ; ) {

/* Get next block to process.  Break out of loop when there are no more blocks to process. */

			int i = (int) atomic_fetch_addin (gwdata->next_block, (data->is_quick ? 1 : 4));
			if (i >= data->num_blks) break;

/* Process the block */

			asm_data->SRCARG = (char *) data->s1 + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			asm_data->SRC2ARG = (char *) data->s2 + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			asm_data->DESTARG = (char *) data->d1 + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			asm_data->DEST2ARG = (char *) data->d2 + i * asm_data->pass1blkdst + (i/4) * asm_data->normblkdst4;
			asm_data->data_addr = (char *) data->d1_carries + i * 128;	// Addr to store dest #1 carries
			asm_data->premult_addr = (char *) data->d2_carries + i * 128;	// Addr to store dest #2 carries
			asm_data->norm_ptr1 = biglit_data_ptr (gwdata, i);
			call_op (data->asm_proc, asm_data);
		}
	}

/* Loop processing AVX blocks */

	else {
		for ( ; ; ) {

/* Get next block to process.  Break out of loop when there are no more blocks to process. */

			int i = (int) atomic_fetch_addin (gwdata->next_block, 1);
			if (i >= data->num_blks) break;

/* Process the block */

			asm_data->SRCARG = (char *) data->s1 + i * asm_data->pass1blkdst;
			asm_data->SRC2ARG = (char *) data->s2 + i * asm_data->pass1blkdst;
			asm_data->DESTARG = (char *) data->d1 + i * asm_data->pass1blkdst;
			asm_data->DEST2ARG = (char *) data->d2 + i * asm_data->pass1blkdst;
			asm_data->data_addr = (char *) data->d1_carries + i * 64;	// Addr to store dest #1 carries
			asm_data->premult_addr = (char *) data->d2_carries + i * 64;	// Addr to store dest #2 carries
			asm_data->premult_prefetch = (char *) asm_data->norm_grp_mults +
				(i / asm_data->count2) * (gwdata->ZERO_PADDED_FFT ? 320 : 640);			// Addr of group multipliers
			asm_data->norm_ptr1 = (char *) asm_data->norm_biglit_array + i * asm_data->normval3;	// Addr of big/lit flags
			call_op (data->asm_proc, asm_data);
		}
	}
}

/* Copy one gwnum to another gwnum */

void gwcopy (			/* Copy a gwnum */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source */
	gwnum	d)		/* Dest */
{

/* MMGW general mod requires special handling -- there may be two gwnums to copy */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwcopy (gwdata->cyclic_gwdata, s, d);
		if (FFT_state (s) != NOT_FFTed) gwcopy (gwdata->negacyclic_gwdata, negacyclic_gwnum (gwdata, s), negacyclic_gwnum (gwdata, d));
		return;
	}

/* There is one piece of information that must not be copied over.  The allocation flags/index at ((uint32_t *) d)[-8] used to free gwnum memory. */
/* NOTE: we used to load this value, overwrite it with memmove, then restore it.  This did not work in a multi-threaded environment when gwfree_freeable */
/* changed the value after we loaded it and before we restored it. */

/* Copy the header before the data we are preserving */

	memmove ((char *) d - GW_HEADER_SIZE (gwdata), (char *) s - GW_HEADER_SIZE (gwdata), GW_HEADER_SIZE (gwdata) - 8 * sizeof (uint32_t));

/* Copy the header after the data we are preserving along with all the gwnum data */

	if (gwdata->num_threads > 1)
		multithread_op (gwdata, s, NULL, d, NULL, NULL, TRUE);
	else
		memmove ((char *) &(((uint32_t *) d)[-7]), (char *) &(((uint32_t *) s)[-7]), 7 * sizeof (uint32_t) + gwnum_datasize (gwdata));

/* Update counts */

	gwdata->read_count += 1;
	gwdata->write_count += 1;
}

/*********************************************************/
/* Wrapper routines for the add and sub assembly code    */
/*********************************************************/

void gwadd3o (			/* Add two numbers normalizing if needed */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1 */
	gwnum	s2,		/* Source #2 */
	gwnum	d,		/* Destination */
	int	options)      
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	float	new_normcnt;

	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);

/* Sanity check the options */

	ASSERTG (!(options & GWADD_DELAY_NORMALIZE) || !(options & GWADD_FORCE_NORMALIZE));	// Both options cannot be set
	// Force normalize, delay normalize, guaranteed OK, or usage must be set
	ASSERTG (options & (GWADD_FORCE_NORMALIZE | GWADD_DELAY_NORMALIZE | GWADD_GUARANTEED_OK | GWADD_SQUARE_INPUT | GWADD_MUL_INPUT | GWADD_ADD_INPUT));

/* If the two FFT states are not the same, make them so */

	if (options & GWADD_FORCE_NORMALIZE) {
		gwunfft (gwdata, s1, s1);
		gwunfft (gwdata, s2, s2);
	}
	else if (FFT_state (s1) != FFT_state (s2)) {
		gwfft (gwdata, s1, s1);
		gwfft (gwdata, s2, s2);
	}

/* MMGW general must be handled differently */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwadd3o (gwdata->cyclic_gwdata, s1, s2, d, options);
		if (FFT_state (s1) != NOT_FFTed)
			gwadd3o (gwdata->negacyclic_gwdata, negacyclic_gwnum (gwdata, s1), negacyclic_gwnum (gwdata, s2), negacyclic_gwnum (gwdata, d), options);
		return;
	}

/* Calculate the new unnormalized add count if we don't do a normalized add. */
/* Non-random input data will eat up a full extra FFT output bit. */

	if (options & GWADD_NON_RANDOM_DATA) new_normcnt = eb_to_numadds (numadds_to_eb (unnorms (s1)) + numadds_to_eb (unnorms (s2)) + 1.0f);
	else new_normcnt = unnorms (s1) + unnorms (s2) + 1.0f;
	if (options & GWADD_GUARANTEED_OK) new_normcnt = 0.0f;

/* If either input is partially or fully FFTed, we cannot normalize the result.  Use a different assembly routine. */

	if (FFT_state (s1) != NOT_FFTed || FFT_state (s2) != NOT_FFTed) {

/* If this is a zero-padded FFT, then also add the 7 copied doubles in the gwnum header */

		if (gwdata->ZERO_PADDED_FFT) {
			d[-5] = s1[-5] + s2[-5];
			d[-6] = s1[-6] + s2[-6];
			d[-7] = s1[-7] + s2[-7];
			d[-8] = s1[-8] + s2[-8];
			d[-9] = s1[-9] + s2[-9];
			d[-10] = s1[-10] + s2[-10];
			d[-11] = s1[-11] + s2[-11];
		}

/* Do an AVX-512 or two-pass AVX addquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d, NULL, addr_gw_addf (gwdata), TRUE);
		}

/* Do the add the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d;
			gw_addf (gwdata, asm_data);
		}

/* Update the count of unnormalized adds and subtracts */
/* Copy the has-been-completely/partially-FFTed flag */

		unnorms (d) = new_normcnt;
		FFT_state (d) = FFT_state (s1);
	}

/* If we can do an addquick, do that - it should be faster. */
/* The safety tests depend on the intended usage of the result. */

	else if ((options & GWADD_DELAY_NORMALIZE) ||
		 (!(options & GWADD_FORCE_NORMALIZE) &&
		  ((options & GWADD_SQUARE_INPUT) ? square_safe (gwdata, new_normcnt) :
		   (options & GWADD_ADD_INPUT) ? addmul_safe (gwdata, new_normcnt, 1, 1) :
		   mul_safe (gwdata, new_normcnt, 1)))) {

/* Do an AVX-512 or two-pass AVX addquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d, NULL, addr_gw_addq (gwdata), TRUE);
		}

/* Do the addquick the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d;
			gw_addq (gwdata, asm_data);
		}

/* Update the count of unnormalized adds and subtracts */
/* Set the has-been-completely/partially-FFTed flag */

		unnorms (d) = new_normcnt;
		FFT_state (d) = NOT_FFTed;
	}

/* Do a normalized add */

	else {

/* Do an AVX-512 or two-pass AVX add */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d, NULL, addr_gw_add (gwdata), FALSE);
		}

/* Do the add the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d;
			gw_add (gwdata, asm_data);
		}

/* Reset the unnormalized adds and subtracts count, clear the has-been-completely/partially-FFTed flag,  */

		unnorms (d) = 0.0f;
		FFT_state (d) = NOT_FFTed;
	}

/* Update counters */

	gwdata->read_count += (s1 == s2) ? 1 : 2;
	gwdata->write_count += 1;
}

void gwsub3o (			/* Compute s1 - s2 normalizing if needed */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1 */
	gwnum	s2,		/* Source #2 */
	gwnum	d,		/* Destination */
	int	options)      
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	float	new_normcnt;

	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);

/* Sanity check the options */

	ASSERTG (!(options & GWADD_DELAY_NORMALIZE) || !(options & GWADD_FORCE_NORMALIZE));	// Both options cannot be set
	// Force normalize, delay normalize, guaranteed OK, or usage must be set
	ASSERTG (options & (GWADD_FORCE_NORMALIZE | GWADD_DELAY_NORMALIZE | GWADD_GUARANTEED_OK | GWADD_SQUARE_INPUT | GWADD_MUL_INPUT | GWADD_ADD_INPUT));

/* If the two FFT states are not the same, make them so */

	if (options & GWADD_FORCE_NORMALIZE) {
		gwunfft (gwdata, s1, s1);
		gwunfft (gwdata, s2, s2);
	}
	else if (FFT_state (s1) != FFT_state (s2)) {
		gwfft (gwdata, s1, s1);
		gwfft (gwdata, s2, s2);
	}

/* MMGW general must be handled differently */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwsub3o (gwdata->cyclic_gwdata, s1, s2, d, options);
		if (FFT_state (s1) != NOT_FFTed)
			gwsub3o (gwdata->negacyclic_gwdata, negacyclic_gwnum (gwdata, s1), negacyclic_gwnum (gwdata, s2), negacyclic_gwnum (gwdata, d), options);
		return;
	}

/* Calculate the new unnormalized add count if we don't do a normalized add. */
/* Non-random input data will eat up a full extra output bit. */

	if (options & GWADD_NON_RANDOM_DATA) new_normcnt = eb_to_numadds (numadds_to_eb (unnorms (s1)) + numadds_to_eb (unnorms (s2)) + 1.0f);
	else new_normcnt = unnorms (s1) + unnorms (s2) + 1.0f;
	if (options & GWADD_GUARANTEED_OK) new_normcnt = 0.0f;

/* If either input is partially or fully FFTed, we cannot normalize the result.  Use a different assembly routine. */

	if (FFT_state (s1) != NOT_FFTed || FFT_state (s2) != NOT_FFTed) {

/* If this is a zero-padded FFT, then also subtract the 7 copied doubles in the gwnum header */

		if (gwdata->ZERO_PADDED_FFT) {
			d[-5] = s1[-5] - s2[-5];
			d[-6] = s1[-6] - s2[-6];
			d[-7] = s1[-7] - s2[-7];
			d[-8] = s1[-8] - s2[-8];
			d[-9] = s1[-9] - s2[-9];
			d[-10] = s1[-10] - s2[-10];
			d[-11] = s1[-11] - s2[-11];
		}

/* Do an AVX-512 or two-pass AVX subquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s2, s1, d, NULL, addr_gw_subf (gwdata), TRUE);
		}

/* Do the subtract the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s2;
			asm_data->SRC2ARG = s1;
			asm_data->DESTARG = d;
			gw_subf (gwdata, asm_data);
		}

/* Update the count of unnormalized adds and subtracts */
/* Copy the has-been-completely/partially-FFTed flag */

		unnorms (d) = new_normcnt;
		FFT_state (d) = FFT_state (s1);
	}

/* If we can do an subquick, do that - it should be faster. */
/* The safety tests depend on the intended usage of the result. */

	else if ((options & GWADD_DELAY_NORMALIZE) ||
		 (!(options & GWADD_FORCE_NORMALIZE) &&
		  ((options & GWADD_SQUARE_INPUT) ? square_safe (gwdata, new_normcnt) :
		   (options & GWADD_ADD_INPUT) ? addmul_safe (gwdata, new_normcnt, 1, 1) :
		   mul_safe (gwdata, new_normcnt, 1)))) {

/* Do an AVX-512 or two-pass AVX subquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s2, s1, d, NULL, addr_gw_subq (gwdata), TRUE);
		}

/* Do the subtract the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s2;
			asm_data->SRC2ARG = s1;
			asm_data->DESTARG = d;
			gw_subq (gwdata, asm_data);
		}

/* Update the count of unnormalized adds and subtracts */
/* Set the has-been-completely/partially-FFTed flag */

		unnorms (d) = new_normcnt;
		FFT_state (d) = NOT_FFTed;
	}

/* Do a normalized subtract */

	else {

/* Do an AVX-512 or two-pass AVX subtract */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s2, s1, d, NULL, addr_gw_sub (gwdata), FALSE);
		}

/* Do the add the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s2;
			asm_data->SRC2ARG = s1;
			asm_data->DESTARG = d;
			gw_sub (gwdata, asm_data);
		}

/* Reset the unnormalized adds and subtracts count, clear the has-been-completely/partially-FFTed flag. */

		unnorms (d) = 0.0f;
		FFT_state (d) = NOT_FFTed;
	}

/* Update counters */

	gwdata->read_count += 2;
	gwdata->write_count += 1;
}

void gwaddsub4o (		/* Add & sub two nums normalizing if needed */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1 */
	gwnum	s2,		/* Source #2 */
	gwnum	d1,		/* Destination #1 */
	gwnum	d2,		/* Destination #2 */
	int	options)
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	float	new_normcnt;

	ASSERTG (unnorms (s1) >= 0.0f);
	ASSERTG (unnorms (s2) >= 0.0f);
	ASSERTG (FFT_state (s1) != FFTed_FOR_FMA && FFT_state (s2) != FFTed_FOR_FMA);

/* Sanity check the options */

	ASSERTG (!(options & GWADD_DELAY_NORMALIZE) || !(options & GWADD_FORCE_NORMALIZE));	// Both options cannot be set
	// Force normalize, delay normalize, guaranteed OK, or usage must be set
	ASSERTG (options & (GWADD_FORCE_NORMALIZE | GWADD_DELAY_NORMALIZE | GWADD_GUARANTEED_OK | GWADD_SQUARE_INPUT | GWADD_MUL_INPUT | GWADD_ADD_INPUT));

/* If the two FFT states are not the same, make them so */

	if (options & GWADD_FORCE_NORMALIZE) {
		gwunfft (gwdata, s1, s1);
		gwunfft (gwdata, s2, s2);
	}
	else if (FFT_state (s1) != FFT_state (s2)) {
		gwfft (gwdata, s1, s1);
		gwfft (gwdata, s2, s2);
	}

/* MMGW general must be handled differently */

	if (gwdata->GENERAL_MMGW_MOD) {
		gwaddsub4o (gwdata->cyclic_gwdata, s1, s2, d1, d2, options);
		if (FFT_state (s1) != NOT_FFTed)
			gwaddsub4o (gwdata->negacyclic_gwdata, negacyclic_gwnum (gwdata, s1), negacyclic_gwnum (gwdata, s2),
				    negacyclic_gwnum (gwdata, d1), negacyclic_gwnum (gwdata, d2), options);
		return;
	}

/* Calculate the new unnormalized add count if we don't do a normalized add. */
/* Non-random input data will eat up a full extra output bit. */

	if (options & GWADD_NON_RANDOM_DATA) new_normcnt = eb_to_numadds (numadds_to_eb (unnorms (s1)) + numadds_to_eb (unnorms (s2)) + 1.0f);
	else new_normcnt = unnorms (s1) + unnorms (s2) + 1.0f;
	if (options & GWADD_GUARANTEED_OK) new_normcnt = 0.0f;

/* If either input is partially or fully FFTed, use a different routine */

	if (FFT_state (s1) != NOT_FFTed || FFT_state (s2) != NOT_FFTed) {

/* If this is a zero-padded FFT, then also add & sub the 7 copied doubles in the gwnum header. */
/* Copy data to temporaries first in case s1, s2 pointers are equal to the d1, d2 pointers! */

		if (gwdata->ZERO_PADDED_FFT) {
			double	v1, v2;
			v1 = s1[-5]; v2 = s2[-5]; d1[-5] = v1 + v2; d2[-5] = v1 - v2;
			v1 = s1[-6]; v2 = s2[-6]; d1[-6] = v1 + v2; d2[-6] = v1 - v2;
			v1 = s1[-7]; v2 = s2[-7]; d1[-7] = v1 + v2; d2[-7] = v1 - v2;
			v1 = s1[-8]; v2 = s2[-8]; d1[-8] = v1 + v2; d2[-8] = v1 - v2;
			v1 = s1[-9]; v2 = s2[-9]; d1[-9] = v1 + v2; d2[-9] = v1 - v2;
			v1 = s1[-10]; v2 = s2[-10]; d1[-10] = v1 + v2; d2[-10] = v1 - v2;
			v1 = s1[-11]; v2 = s2[-11]; d1[-11] = v1 + v2; d2[-11] = v1 - v2;
		}

/* Do an AVX-512 or two-pass AVX addsubquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d1, d2, addr_gw_addsubf (gwdata), TRUE);
		}

/* Do the add & subtract the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d1;
			asm_data->DEST2ARG = d2;
			gw_addsubf (gwdata, asm_data);
		}

/* Update the counts of unnormalized adds and subtracts */
/* Copy the has-been-completely/partially-FFTed flag */

		unnorms (d1) = unnorms (d2) = new_normcnt;
		FFT_state (d1) = FFT_state (d2) = FFT_state (s1);
	}

/* If we can do an addsubquick, do that - it should be faster. */
/* The safety tests depend on the intended usage of the result. */

	else if ((options & GWADD_DELAY_NORMALIZE) ||
		 (!(options & GWADD_FORCE_NORMALIZE) &&
		  ((options & GWADD_SQUARE_INPUT) ? square_safe (gwdata, new_normcnt) :
		   (options & GWADD_ADD_INPUT) ? addmul_safe (gwdata, new_normcnt, 1, 1) :
		   mul_safe (gwdata, new_normcnt, 1)))) {

/* Do an AVX-512 or two-pass AVX addsubquick */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d1, d2, addr_gw_addsubq (gwdata), TRUE);
		}

/* Do the add & subtract the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d1;
			asm_data->DEST2ARG = d2;
			gw_addsubq (gwdata, asm_data);
		}

/* Update the count of unnormalized adds and subtracts */
/* Set the has-been-completely/partially-FFTed flag */

		unnorms (d1) = unnorms (d2) = new_normcnt;
		FFT_state (d1) = FFT_state (d2) = NOT_FFTed;
	}

/* Do a normalized addsub */

	else {

/* Do an AVX-512 or two-pass AVX add/sub */

		if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
			multithread_op (gwdata, s1, s2, d1, d2, addr_gw_addsub (gwdata), FALSE);
		}

/* Do the add & subtract the old way -- single-threaded in assembly code */

		else {
			asm_data->SRCARG = s1;
			asm_data->SRC2ARG = s2;
			asm_data->DESTARG = d1;
			asm_data->DEST2ARG = d2;
			gw_addsub (gwdata, asm_data);
		}

/* Reset the unnormalized adds and subtracts count, clear the has-been-completely/partially-FFTed flags */

		unnorms (d1) = 0.0f;
		unnorms (d2) = 0.0f;
		FFT_state (d1) = NOT_FFTed;
		FFT_state (d2) = NOT_FFTed;
	}

/* Update counters */

	gwdata->read_count += 2;
	gwdata->write_count += 2;
}

/* Routine to add a small number to a gwnum.  Some day, I might optimize this routine for the cases where */
/* just one or two doubles need to be modified in the gwnum. */

void gwsmalladd (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	int64_t	addin,		/* Small value to add to g */
	gwnum	g)		/* Gwnum to add a value into */
{

/* Assert unnormalized add count valid, input not completely/partially FFTed. */

	ASSERTG (unnorms (g) >= 0.0f);
	ASSERTG (FFT_state (g) == NOT_FFTed);

/* Small numbers adds can be optimized for many moduli only adding into a few FFT words */

	if (!gwdata->GENERAL_MMGW_MOD && (gwdata->k == 1.0 || labs (gwdata->c) == 1)) {
		int64_t	low_addin;
		gwiter iter;
		int32_t maxval1 = intpow (gwdata->b, gwdata->NUM_B_PER_SMALL_WORD);
		int32_t maxval2 = maxval1 * gwdata->b;

/* To make the mod k*b^n+c step faster, gwnum's are pre-multiplied by 1/k.  Case 1 (k*b^n-1): Inverse of k is b^n.  Case 2 (k*b^n+1): Inverse of k is -b^n. */
/* Since the value we are adding to the top FFT words can be greater than k we must divide value by k, adding the quotient to the lowest FFT words. */
/* The remainder gets added to the upper FFT words. */

		if (gwdata->k > 1.0) {
			int64_t quotient, remainder, high_addin;

			quotient = addin / (int64_t) gwdata->k;
			remainder = addin - quotient * (int64_t) gwdata->k;
			low_addin = quotient;
			high_addin = (gwdata->c == -1) ? remainder : -remainder;

/* Add the high_addin value across the top FFT words */

			unsigned long word, bit_in_word;
			bitaddr (gwdata, gwdata->n, &word, &bit_in_word);
			for (gwiter_init (gwdata, &iter, g, word); high_addin && gwiter_index (&iter) < gwdata->FFTLEN; gwiter_next (&iter), bit_in_word = 0) {

				// In rare cases there is no data stored in an FFT word (NUM_B_PER_SMALL_WORD = 0)
				int32_t maxval = gwiter_is_big_word (&iter) ? maxval2 : maxval1;
				if (maxval == 1) continue;

				// Get the value to add into
				int32_t	value;
				gwiter_get_fft_value (&iter, &value);

				// Add part or all of high_addin to the FFT word
				int32_t scale = bit_in_word ? intpow (gwdata->b, bit_in_word) : 1;
				int32_t scaledmaxval = maxval / scale;
				quotient = high_addin / scaledmaxval;
				value += (int32_t) (high_addin - quotient * scaledmaxval) * scale;
				high_addin = quotient;

				// FFT words use balanced representation
				if (gwiter_index (&iter) != gwdata->FFTLEN - 1) {
					if (value > (maxval >> 1)) {
						value = value - maxval;
						high_addin++;
					}
					if (value < -(maxval >> 1)) {
						value = value + maxval;
						high_addin--;
					}
				}

				gwiter_set_fft_value (&iter, value);
			}
		}

/* If k is 1, apply the entire addin to the low FFT words */

		else
			low_addin = addin;

/* Spread the low_addin value across the lowest FFT words as necessary */

		for (gwiter_init_zero (gwdata, &iter, g); low_addin; gwiter_next (&iter)) {

			// In rare cases there is no data stored in an FFT word (NUM_B_PER_SMALL_WORD = 0)
			int32_t maxval = gwiter_is_big_word (&iter) ? maxval2 : maxval1;
			if (maxval == 1) continue;

			// Get the value to add into
			int32_t	value;
			gwiter_get_fft_value (&iter, &value);

			// If addin/carry is tiny, just add it in without carry propagation
			if (low_addin >= -7 && low_addin <= 7) {
				gwiter_set_fft_value (&iter, value + (int32_t) low_addin);
				break;
			}

			// Carefully apply low_addin to not overflow int64_t data type
			int64_t quotient = low_addin / maxval;
			value += (int32_t) (low_addin - quotient * maxval);
			low_addin = quotient;

			// FFT words use balanced representation
			if (value > (maxval >> 1)) {
				value = value - maxval;
				low_addin++;
			}
			if (value < -(maxval >> 1)) {
				value = value + maxval;
				low_addin--;
			}

			gwiter_set_fft_value (&iter, value);
		}
	}

/* Hard cases, emulate the addin */

	else {
		gwnum tmp = gwalloc (gwdata);
		s64togw (gwdata, addin, tmp);
		gwadd3o (gwdata, g, tmp, g, GWADD_FORCE_NORMALIZE);
		gwfree (gwdata, tmp);
	}
}

/* This routine multiplies a gwnum by a small positive value.  This lets us apply some */
/* optimizations that cannot be performed by a full FFT multiplication. */

void gwsmallmul (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	mult,		/* Small value to multiply g by */
	gwnum	g)		/* Gwnum to multiply */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;

/* Assert unnormalized add count valid, input not completely/partially FFTed. */

	ASSERTG (unnorms (g) >= 0.0f);
	ASSERTG (FFT_state (g) == NOT_FFTed);

/* MMGW general must be handled differently */

	if (gwdata->GENERAL_MMGW_MOD) {
		if (FFT_state (g) != NOT_FFTed) gwsmallmul (gwdata->negacyclic_gwdata, mult, negacyclic_gwnum (gwdata, g));
		gwsmallmul (gwdata->cyclic_gwdata, mult, g);
		return;
	}

/* The x87 assembly version won't spread carries over multiple words.  Also, the x87 assembly version won't guard against carries out of the critical */
/* top words in a zero-padded number.  Also, SSE2 has trouble with sparse FFTs where there are not enough FFT words to spread the wraparound carry to */
/* (example: 31*28619^8930+1).  Also, guard against a multiplication that generates a carry out of the top 4 words of a zero padded FFT (AVX example: */
/* 158389*40^158389+1 with a larger_fft_count of 7).  Use a simple brute-force implementation instead. */

	if ((! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX | CPU_SSE2)) && (mult > 1024.0 || gwdata->ZERO_PADDED_FFT)) ||
	    (! (gwdata->cpu_flags & (CPU_AVX512F | CPU_AVX)) && gwdata->avg_num_b_per_word < 0.5 && mult > gwdata->b) ||
	    (gwdata->ZERO_PADDED_FFT && gwdata->k * mult > pow (gwdata->b, floor (4 * gwdata->avg_num_b_per_word)) / 2.0)) {
		gwnum	tmp;
		tmp = gwalloc (gwdata);
		if (mult == 1.0);
		else if (mult == 2.0) {
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 3.0) {
			gwadd3o (gwdata, g, g, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, tmp, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 4.0) {
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 5.0) {
			gwadd3o (gwdata, g, g, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, tmp, tmp, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, tmp, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 6.0) {
			gwadd3o (gwdata, g, g, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, tmp, g, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 8.0) {
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, g, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else if (mult == 9.0) {
			gwadd3o (gwdata, g, g, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, tmp, tmp, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, tmp, tmp, tmp, GWADD_NON_RANDOM_DATA | GWADD_DELAY_NORMALIZE);
			gwadd3o (gwdata, g, tmp, g, GWADD_NON_RANDOM_DATA | GWADD_SQUARE_INPUT);
		}
		else {
			dbltogw (gwdata, mult, tmp);
			gwmul3 (gwdata, tmp, g, g, 0);
		}
		gwfree (gwdata, tmp);
	}

/* Do an AVX-512 or two-pass AVX small mul */

	else if ((gwdata->cpu_flags & CPU_AVX512F) || (gwdata->cpu_flags & CPU_AVX && gwdata->PASS2_SIZE)) {
		asm_data->DBLARG = mult;
		multithread_op (gwdata, g, NULL, g, NULL, addr_gw_muls (gwdata), FALSE);
	}

/* Do the small mul the old way -- single-threaded in assembly code */

	else {
		asm_data->DESTARG = g;
		asm_data->DBLARG = mult;
		gw_muls (gwdata, asm_data);
	}

/* Update counts */

	unnorms (g) = 0.0f;
	gwdata->read_count += 1;
	gwdata->write_count += 1;

/* If the number has gotten too large (high words should all be weighted -1 or 0) then emulate general mod with 2 multiplies */

	if (gwdata->GENERAL_MOD &&
	    (* (double *) ((char *) g + gwdata->GW_GEN_MOD_MAX_OFFSET) <= -2.0 ||
	     * (double *) ((char *) g + gwdata->GW_GEN_MOD_MAX_OFFSET) > 0.0))
		emulate_mod (gwdata, g);
}
