/*----------------------------------------------------------------------
| This file contains various utility routines that may be used by gwnum setup.
|
|  Copyright 2011-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWTABLES_H
#define _GWTABLES_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* This structure declares all the global constants and temporaries needed */
/* by the assembly code.  It is allocated and initialized with C code. */
/* In 32-bit mode, the area before the structure also serves as stack space. */
/* This lets the assembly code which is starved for registers use the stack */
/* pointer to access the data in this structure. */

#ifdef X86_64
#define NEW_STACK_SIZE	0
#else
#define NEW_STACK_SIZE	(8192+256)
#endif

/* Pointers are 4 bytes in 32-bit mode, so we group them in blocks of 16 to */
/* occupy a full cache line (or two cache lines in 64-bit mode).  Similarly, */
/* doubles are grouped in blocks of 8. 32-bit ints are grouped in blocks of 16. */

struct gwasm_data {
	void	*DESTARG;		/* Function destination argument */
	intptr_t DIST_TO_FFTSRCARG;	/* SRCARG - DESTARG */
	intptr_t DIST_TO_MULSRCARG;	/* SRC2ARG - DESTARG */
	void	*NORMRTN;		/* Post-multiply normalization routine */
	void	*SAVED_RSP;		/* Saved stack pointer in 32-bit mode */
	void	*SRCARG;		/* Function argument */
	void	*SRC2ARG;		/* Function argument */
	void	*DEST2ARG;		/* Function argument */
	void	*data_addr;		/* FFT data to work on this pass */
	void	*data_prefetch;		/* FFT data to prefetch for next pass */
	void	*premult_addr;		/* Premult data to use this pass */
	void	*premult_prefetch;	/* Premult data to prefetch for next pass */
	void	*pass1_wake_up_threads;	/* Callback routine to wake up auxiliary threads */
	void	*pass1_pre_carries;	/* Callback routine to prior to normalizing a block of data */
	void	*pass1_post_carries;	/* Callback routine to after normalizing a block of data */
	void	*pass1_get_next_block;	/* Callback routine to get next block number for thread to process */

	double	DBLARG;			/* Function argument */
	uint32_t NUMARG;		/* Gwcopyzero assembly arg */
	uint32_t FFTLEN;		/* The FFT size we are using */
	double	MAXERR;			/* Convolution error in a multiplication */
	char	NEGACYCLIC_FFT;		/* True if doing a negacyclic FFT */
	char	B_IS_2;			/* True is doing a base-2 FFT */
	char	RATIONAL_FFT;		/* True if bits per FFT word is integer */
	char	ZERO_PADDED_FFT;	/* True if doing a zero pad FFT */
	char	ZPAD_TYPE;		/* 1,2,or 3 words in k (used by zero pad) */
	char	ffttype;		/* Type of fft (1, 2=square, 3, or 4) */
	char	TOP_CARRY_NEEDS_ADJUSTING; /* True when carry out of top word needs adjusting */
	char	SPREAD_CARRY_OVER_EXTRA_WORDS; /* AVX: True when carries must be spread over more than 4 words. */
					/* X87,SSE2: True when carries must be spread over more than 2 words. */
	char	zero_fft;		/* TRUE if zero upper half in normalize */
	char	const_fft;		/* TRUE if mul-by-const in normalize */
	char	add_sub_smallmul_op;	/* TRUE if we are processing carries from an add/sub/smallmul operation */
	char	mul4_opcode;		/* 0 for normal gwmul3, 1 for gwaddmul4, 2 for gwsubmul4 */
	char	aux_initialized;	/* Set to TRUE when an auxiliary thread's asm_data has been initialized */
	char	UNUSED_CHARS[3];
	uint32_t ADDIN_ROW;		/* For adding a constant after multiply */
	uint32_t ADDIN_OFFSET;
	double	ADDIN_VALUE;		/* Value to add in after a multiply and before a mul-by-const */
	double	POSTADDIN_VALUE;	/* Value to add in after a multiply and after a mul-by-const */

	int32_t	thread_num;		/* Thread num - to differentiate main thread from auxiliary threads */
	uint32_t this_block;		/* Block currently being processed */
	uint32_t next_block;		/* Next block to process */
	uint32_t last_pass1_block;	/* Last block to process */
	int32_t	normcount1;
	int32_t	count1;			/* Counter used in common fft code */
	int32_t	count2;
	int32_t	count3;
	int32_t	count4;
	int32_t	count5;
	int32_t	addcount1;		/* Counter used in common fft code */
	int32_t	normval1;		/* Add / sub constants */
	int32_t	normval4;
	int32_t	cache_line_multiplier;	/* Cache line multiplier from jmptab */
	uint32_t BIGLIT_INCR2;		/* Offset to step in big/lit array */		/* Unused in AVX-512 */
	uint32_t BIGLIT_INCR4;		/* Offset to step in big/lit array */		/* Unused in AVX-512 */

	void	*carries;		/* Ptr to array of carries (2 pass FFT) */
	void	*norm_grp_mults;	/* Ptr to array #1 of normalize multipliers */
	void	*norm_col_mults;	/* Ptr to array #2 of normalize multipliers */	/* Unused in AVX-512 */
	void	*norm_biglit_array;	/* Ptr to byte array of big/lit flags */	/* Unused in AVX-512 */
	void	*norm_ptr1;
	union {
		void	*norm_ptr2;	/* Ptr used in normalize code.  Unused in AVX-512 */
		intptr_t normblkdst4;	/* Dist between 4 blocks in normalize code.  Used in one-pass AVX-512 FFTs */
	};
	intptr_t normblkdst;		/* Dist between blocks in normalization code */
	intptr_t normblkdst8;		/* Dist between 8 blocks in normalize code */
	intptr_t normval2;		/* Add / sub temporaries */
	intptr_t normval3;
	void	*scratch_area;		/* Scratch area for pass 1 of SSE2 FFTs */
	void	*plus1_premults;	/* Address of 2^N+1 premultiplier data */	/* Unused in AVX-512 */
	intptr_t fourKBgapsize;		/* Wasted bytes between 4KB data blks */
	intptr_t pass2gapsize;		/* Wasted bytes between pass2 data blks */
	intptr_t pass1blkdst;		/* Dist between blocks in pass 1 */
	intptr_t pass2blkdst;		/* Dist between blocks in pass 2 */

	void	*compressed_biglits;	/* Ptr to big/lit masks; norm_biglit_array indexes into this */
	void	*compressed_fudges;	/* Ptr to fudge masks; norm_fudge_array indexes into this */
	gwhandle *gwdata;		/* Allows callback routines to access gwdata */
	void	*pass2_wake_up_threads;	/* Callback routine to wake up auxiliary threads */
	void	*pass2_get_next_block;	/* Callback routine to get next block number for thread to process */
	void	(*thread_work_routine)(void*); /* Assembly routine to call when auxiliary thread wakes up */
	void	*xsincos_complex;	/* Addr of pass2 complex sin/cos data */
	void	*sincos1;
	void	*sincos2;
	void	*sincos3;
	void	*sincos4;
	void	*sincos5;
	gwthread UNUSED_hyperthread_id;	/* Thread ID of prefetching hyperthread */
	gwevent UNUSED_hyperthread_work_to_do;	/* Event to signal hyperthread to begin prefetching */
	void	*SRC3ARG;		/* Function argument */
	uint32_t *ASM_TIMERS;		/* Timers used for optimizing code */

	uint32_t COPYZERO[8];		/* Offsets to help in gwcopyzero */
	double	K;			/* K */
	double	INVERSE_K;		/* 1/K */
	double	TWO_TO_17;		/* 2^17 */
	double	CARRY_ADJUST1;		/* Adjustment constant #1 in wrapping carry */

	double	CARRY_ADJUST2;		/* Adjustment constant #2 in wrapping carry */
	double	CARRY_ADJUST3;		/* Adjustment constant #3 in wrapping carry */
	double	CARRY_ADJUST4;		/* Adjustment constant #4 in wrapping carry */
	double	CARRY_ADJUST5;		/* Adjustment constant #5 in wrapping carry */
	double	CARRY_ADJUST6;		/* Adjustment constant #6 in wrapping carry */
	double	CARRY_ADJUST7;
	double	CARRY_ADJUST1_HI;
	double	CARRY_ADJUST1_LO;

	uint32_t HIGH_WORD1_OFFSET;	/* Offset of top FFT word */
	uint32_t HIGH_WORD2_OFFSET;	/* Offset of second high FFT word */
	uint32_t HIGH_WORD3_OFFSET;	/* Offset of third high FFT word */
	uint32_t HIGH_SCRATCH1_OFFSET;	/* Offset of top FFT word in scratch area */
	uint32_t HIGH_SCRATCH2_OFFSET;	/* Offset of second highest FFT word */
	uint32_t HIGH_SCRATCH3_OFFSET;	/* Offset of third highest FFT word */
	double	ZPAD_INVERSE_K6;	/* Zero padded FFT constants */
	double	ZPAD_K6_HI;
	double	ZPAD_K6_MID;
	double	ZPAD_K6_LO;
	double	ZPAD_SHIFT6;

	double	ZPAD_INVERSE_K5;
	double	ZPAD_K5_HI;
	double	ZPAD_K5_MID;
	double	ZPAD_K5_LO;
	double	ZPAD_SHIFT5;
	double	ZPAD_INVERSE_K4;
	double	ZPAD_K4_HI;
	double	ZPAD_K4_MID;

	double	ZPAD_K4_LO;
	double	ZPAD_SHIFT4;
	double	ZPAD_INVERSE_K3;
	double	ZPAD_K3_HI;
	double	ZPAD_K3_MID;
	double	ZPAD_K3_LO;
	double	ZPAD_SHIFT3;
	double	ZPAD_INVERSE_K2;

	double	ZPAD_K2_HI;
	double	ZPAD_K2_MID;
	double	ZPAD_K2_LO;
	double	ZPAD_SHIFT2;
	double	ZPAD_INVERSE_K1;
	double	ZPAD_K1_HI;
	double	ZPAD_K1_LO;
	double	ZPAD_SHIFT1;

	double	ZPAD0_6[7];		/* 7 ZPAD doubles */
	double	ZPAD_LSW_ADJUST;	/* Multiplier for ADDIN_VALUE when ZERO_PADDED k=1, abs(c)!=1 */

	double	ttmp_ff_inv;		/* Inverse FFT adjust (2/FFTLEN) */
	double	UNUSED_DOUBLES[7];

	union {
	    struct zmm_data {
		intptr_t ZMM_SRC_INCR;		/* Increments to get up to eight consecutive source values */
		void	*ZMM_PASS2_ROUTINE;	/* Routine to call to do pass 2 of the FFT */
		void	*ZMM_CARRIES_ROUTINE;	/* Routine to propagate carries in 2-pass FFTs */
		void	*ZMM_OP_CARRIES_ROUTINE; /* Routine to propagate carries after an add/sub/addsub/smallmul op in 2-pass FFTs */

		double	ZMM_MINUS_C;		/* -c stored as double */
		double	ZMM_MULCONST;
		double	ZMM_MINUS_C_TIMES_MULCONST;
		char	ZMM_FIRST_BIGLIT_VALUES[1];  /* Big/lit flags for first 8 values */
		char	ZMM_UNUSED_CHARS[7];

		double	ZMM_RNDVAL;		/* Used to round double to integer */
		int32_t	ZMM_ABSVAL[2];		/* Used to compute absolute values */
		double	ZMM_LARGE_BASE;		/* Used to round double to integer */
		double	ZMM_LARGE_BASE_INVERSE;	/* Used to round double to integer */
		double	ZMM_RNDVAL_TIMES_LARGE_BASE; /* Used to round double to integer */
		double	ZMM_RNDVAL_OVER_LARGE_BASE; /* Used to round double to integer */
		double	ZMM_SMALL_BASE;		/* Used to round double to integer */
		double	ZMM_SMALL_BASE_INVERSE;	/* Used to round double to integer */
		double	ZMM_RNDVAL_TIMES_SMALL_BASE; /* Used to round double to integer */
		double	ZMM_RNDVAL_OVER_SMALL_BASE; /* Used to round double to integer */

		double	ZMM_K_LO;		/* k, bottom big word bits */
		double	ZMM_K_HI_OVER_SMALL_BASE; /* k, top bits */
		double	ZMM_K_HI_OVER_LARGE_BASE; /* k, top bits */
		double	ZMM_K_TIMES_MULCONST_LO; /* k*mulconst, low big word bits */
		double	ZMM_K_TIMES_MULCONST_HI_OVER_SMALL_BASE; /* k*mulconst, top bits */
		double	ZMM_K_TIMES_MULCONST_HI_OVER_LARGE_BASE; /* k*mulconst, top bits */

		// Next 4 values must be in this order and on a 256-bit boundary
		double	ZMM_ONE;		/* 1.0 - used in 128-real and expanding reduced sin/cos */
		double	ZMM_P383;		/* Used in 128-real, 16-real, 16-complex, 32-real macros */
		double	ZMM_SQRTHALF;		/* SQRT(0.5) - used in 128-real and many other macros */
		double	ZMM_P383_1;		/* Used in 128-real macros */
		// End of 4 values that must be in a particular order and on a 256-bit boundary

		double	ZMM_TWO;		/* 2.0 - used in 64-complex macros */
		double	ZMM_HALF;		/* 0.5, used in 12-real, 12-complex, 24-real, 128-real macros */
		double	ZMM_P924_P383;		/* Used in 128-real, 16-real, 16-complex, 32-real macros */

		double	ZMM_P981_P195;		/* Used in 32-real macros */
		double	ZMM_P195;		/* Used in 32-real macros */
		double	ZMM_P831_P556;		/* Used in 32-real macros */
		double	ZMM_P556_P195;		/* Used in 32-real macros */

		double	ZMM_SQRT2;		/* Used in 8-complex, 16-real macros (special versions to reduce roundoff error) */

		double	ZMM_P866;		/* Used in 6-complex, 12-real, 12-complex, 24-real macros */
		double	ZMM_P259_P707;		/* Used in 24-real macros */
		double	ZMM_P966_P707;		/* Used in 24-real macros */

		double	ZMM_P309;		/* Used in 5-complex, 10-real, 10-complex, 20-real macros */
		double	ZMM_P809;		/* Used in 5-complex, 10-real, 10-complex, 20-real macros */
		double	ZMM_P951;		/* Used in 5-complex, 10-real, 10-complex, 20-real macros */
		double	ZMM_P588_P951;		/* Used in 5-complex, 10-real, 10-complex, 20-real macros */

		double	UNUSED_P623;
		double	UNUSED_P901;
		double	UNUSED_P975;
		double	UNUSED_P223;
		double	ZMM_P434_P975;		/* Used in 7-complex, 14-real macros */
		double	ZMM_P782_P975;		/* Used in 7-complex, 14-real macros */
		double	ZMM_P901_P975;		/* Used in 7-complex, 14-real macros */
		double	ZMM_P623_P975;		/* Used in 7-complex, 14-real macros */
		double	ZMM_P223_P975;		/* Used in 7-complex, 14-real macros */
		double	ZMM_P1_P975;		/* Used in 7-complex, 14-real macros */

		double	ZMM_B;			/* Used in first and last levels of two pass FFT */
		double	ZMM_ONE_OVER_B;

		int64_t	ZMM_PERMUTE1;		/* Permute indices for swizzling.  Used in one-pass wrapper, rsc macros */
		int64_t	ZMM_PERMUTE2;		/* Permute indices for swizzling.  Used in one-pass wrapper, rsc macros */

		double	UNUSED_DOUBLES[7];	/* Pad to a cache line */

		double	ZMM_TMPS[512];		/* Space for (clm=4)*ZMM_SCD8 temps generated by zr8_calc_sincos */
	    } zmm;

	    struct ymm_data {
		intptr_t YMM_SRC_INCR1;		/* Increments to get to next source value */
		intptr_t YMM_SRC_INCR2;
		intptr_t YMM_SRC_INCR3;
		intptr_t YMM_SRC_INCR4;
		intptr_t YMM_SRC_INCR5;
		intptr_t YMM_SRC_INCR6;
		intptr_t YMM_SRC_INCR7;
		intptr_t YMM_NORM_INCR1;	/* Increments to get to next ttp/ttmp values */
		intptr_t YMM_NORM_INCR2;
		intptr_t YMM_NORM_INCR3;
		intptr_t YMM_NORM_INCR4;
		intptr_t YMM_NORM_INCR5;
		intptr_t YMM_NORM_INCR6;
		intptr_t YMM_NORM_INCR7;
		void	*YMM_CARRIES_ROUTINE;	/* Routine to propagate carries in 2-pass FFTs */
		void	*YMM_PASS2_ROUTINE;	/* Routine to call to do pass 2 of the FFT */

		double	YMM_MINUS_C[4];	/* -c stored as double */
		double	YMM_HALF[4];	/* 0.5 */

		double	YMM_SQRTHALF[4];/* SQRT(0.5) */
		double	YMM_UNUSED_DOUBLES1[4];

		double	YMM_MAXERR[4];	/* Used in normalization macros */
		int32_t	YMM_ABSVAL[8];	/* Used to compute absolute values */

		double	YMM_BIGVAL[4];	/* Used to round double to integer */
		double	YMM_BIGBIGVAL[4]; /* Used to round to big word multiple */

		double	YMM_NORM012_FF[4]; /* Used in xnorm012 macros (FFTLEN/2) */
		double	YMM_MULCONST[4];

		double	YMM_K_LO[4];	/* k, bottom big word bits */
		double	YMM_K_HI[4];	/* k, top bits */

		double	YMM_K_TIMES_MULCONST_LO[4]; /* k*mulconst, low big word bits */
		double	YMM_K_TIMES_MULCONST_HI[4]; /* k*mulconst, top bits */

		double	YMM_MINUS_C_TIMES_MULCONST[4];
		char	YMM_FIRST_BIGLIT_VALUES[8];  /* Big/lit flags for first 8 values */
		double	UNUSED_YMM_DOUBLES[3];

		double	UNUSED_YMM_DOUBLES2[4];
		double	YMM_P924_P383[4]; /* Used in FMA versions of 16-reals macros */

		double	YMM_P383[4];	/* Used in one-pass negacyclic premultipliers */
		double	YMM_P924[4];	/* and in the sixteen reals macros. */

		double	YMM_P866[4];	/* Used in three-complex building blocks */
		double	YMM_P588[4];	/* Used in five-complex and 20-reals macros */

		double	YMM_P309[4];
		double	YMM_P809[4];

		double	YMM_P951[4];
		double	YMM_P618[4];	/* only used in FMA version of five-complex and 20-reals (.588/.951) */

		double	YMM_ONE[4];	/* 1.0 - used in FMA3 code */
		double	YMM_TWO[4];	/* 2.0 */

		double	YMM_P975[4];	/* Used in radix-28 macros */
		double	YMM_P782[4];

		double	YMM_P623[4];
		double	YMM_P901[4];

		double	YMM_P434[4];
		double	YMM_P223[4];

		double	YMM_P975_P434[4];	/* only used in FMA version of radix-28 macros */
		double	YMM_P782_P434[4];

		double	YMM_P259[4];		/* Used in radix-24 macros */
		double	YMM_P966[4];

		double	YMM_P259_P707[4];	/* Used in FMA versions of radix-24 macros */
		double	YMM_P966_P707[4];

		double	YMM_LIMIT_BIGMAX[192];	/* Normalization constants */
		double	YMM_LIMIT_INVERSE[192];
		double	YMM_TMPS[256];		/* 26 YMM temporaries or space for clm*YMM_SCD8 using sg8cl */
	    } ymm;

	    struct xmm_data {
		void	*sincos6;		/* Must be in same position as x87_data! */
		void	*sincos7;		/* Must be in same position as x87_data! */
		void	*sincos8;		/* Must be in same position as x87_data! */
		void	*sincos9;		/* Must be in same position as x87_data! */
		void	*sincos10;		/* Must be in same position as x87_data! */
		void	*sincos11;		/* Sin/cos table pointer only used by SSE2 home-grown FFTs */
		intptr_t ZPAD_WORD5_OFFSET;	/* Offset of the 5th word */
		intptr_t ZPAD_WORD5_RBP_OFFSET;	/* Offset to 5th word's ttp data */
		void	*pass2_premults;	/* Address of pass 2 premultiplier data for home-grown FFTs */
		void	*UNUSED_XMM_PTRS[7];

		double	XMM_TWO[2];		/* 2.0 */
		double	XMM_HALF[2];		/* 0.5 */
		double	XMM_SQRTHALF[2];	/* SQRT(0.5) */
		double	XMM_SUMOUT[2];		/* Used in normalization macros */

		double	XMM_MAXERR[2];		/* Used in normalization macros */
		int32_t	XMM_ABSVAL[4];		/* Used to compute absolute values */
		double	XMM_BIGVAL[2];		/* Used to round double to integer */
		double	XMM_BIGBIGVAL[2];	/* Used to round to big word multiple */

		double	XMM_BIGVAL_NEG[2];
		double	XMM_MINUS_C[2];		/* -c stored as double */
		double	XMM_NORM012_FF[2];	/* Used in xnorm012 macros (FFTLEN/2) */
		double	XMM_MULCONST[2];

		double	XMM_K_LO[2];		/* k, bottom big word bits */
		double	XMM_K_HI[2];		/* k, top bits */
		double	XMM_K_TIMES_MULCONST_LO[2]; /* k*mulconst, low big word bits */
		double	XMM_K_TIMES_MULCONST_HI[2]; /* k*mulconst, top bits */

		double	XMM_MINUS_C_TIMES_MULCONST[2];
		double	XMM_P309[2];		/* Used in radix-20 macros */
		double	XMM_P809[2];
		double	XMM_P951[2];

		double	XMM_P588[2];
		double	XMM_P618[2];		/* Used in old v25 home-grown PFA-5 macros */
		double	XMM_M809[2];
		double	XMM_M262[2];

		double	XMM_M382[2];
		double	XMM_M162[2];
		double	XMM_P866[2];		/* Used in PFA-6 */
		double	XMM_P924[2];		/* Used in old v25 home-grown PFA-5 macros */

		double	XMM_P383[2];
		double	XMM_M358[2];
		double	XMM_P404[2];
		double	XMM_P445[2];

		double	XMM_P180[2];
		double	XMM_P975[2];		/* Used in radix-28 macros */
		double	XMM_P623[2];
		double	XMM_P901[2];

		double	XMM_P782[2];
		double	XMM_P434[2];
		double	XMM_P223[2];
		double	UNUSED_XMM_DOUBLES[2];

		double	XMM_TMP1_8[16];		/* 8 XMM temporaries */
		double	XMM_LIMIT_BIGMAX[96];	/* Normalization constants */
		double	XMM_LIMIT_INVERSE[96];
		double	XMM_LIMIT_BIGMAX_NEG[96];
		double	XMM_TTP_FUDGE[32];
		double	XMM_TTMP_FUDGE[32];
		double	XMM_COL_MULTS[1024];
	    } xmm;

	    struct x87_data {
		void	*sincos6;		/* Must be in same position as xmm_data! */
		void	*sincos7;		/* Must be in same position as xmm_data! */
		void	*sincos8;		/* Must be in same position as xmm_data! */
		void	*sincos9;		/* Must be in same position as xmm_data! */
		void	*sincos10;		/* Must be in same position as xmm_data! */
		void	*zpad_addr;		/* Address of next ZPAD word to write */
		void	*pass2_premults;	/* Address of pass 2 premultiplier data for home-grown FFTs */
		char	POSTFFT;		/* True if assembly code can start the FFT process on the result of a multiply */
		char	UNUSED_X87_CHARS[3];
		void	*UNUSED_X87_PTRS[8];

		double	SQRTHALF;		/* SQRT(0.5) */
		double	SUMOUT;			/* Used in normalization macros */
		float	BIGVAL;			/* Rounding value */
		float	BIGBIGVAL;
		double	MINUS_C;		/* -c */
		double	NORM012_FF;		/* Used in xnorm012 macros (FFTLEN/2) */
		double	MULCONST;
		double	K_TIMES_MULCONST_LO;	/* k*mulconst, low big word bits */
		double	K_TIMES_MULCONST_HI;	/* k*mulconst, top bits */

		double	MINUS_C_TIMES_MULCONST;
		double	K_TIMES_MULCONST_HI_1;	/* XMM_K_TIMES_MULCONST_HI, low bits */
		double	K_TIMES_MULCONST_HI_2;	/* XMM_K_TIMES_MULCONST_HI, top bits */
		double	K_HI;			/* Upper bits of K */
		double	K_LO;			/* Lower bits of K */
		double	K_HI_1;			/* K_HI, bottom big word bits */
		double	K_HI_2;			/* K_HI, top bits */
		double	ALT_K_HI;		/* Alternative upper bits of K */

		double	ALT_K_LO;		/* Alternative lower bits of K */
		double	P309;
		double	M809;
		double	M262;
		double	M382;
		double	P951;
		double	P588;
		double	M162;

		double	P618;
		double	P623;
		double	M358;
		double	P404;
		double	P975;
		double	P445;
		double	P180;
		double	M223;

		double	M901;
		double	M691;
		double	P866;
		double	P433;
		double	P577;
		double	UNUSED_X87_DOUBLES[3];

		float	P25;			/* 0.25 */
		float	P75;			/* 0.75 */
		float	P3;			/* 3.0 */
		float	HALF;			/* 0.5 */
		double	TMP1_6[6];		/* 6 temporaries */

		double	LIMIT_BIGMAX[32];	/* Normalization constants */
		double	LIMIT_INVERSE[32];
		double	LIMIT_BIGMAX_NEG[32];
		double	TTP_FUDGE[32];
		double	TTMP_FUDGE[32];
	    } x87;
	} u;
};

/* Redefinitions for better AVX-512 code readability */

/* Routines to build sin/cos, weights tables */

unsigned long pow_two_above_or_equal (unsigned long n);

double *zr4dwpn_build_pass1_table (gwhandle *gwdata, double *table);
double *zr4dwpn_build_fixed_pass1_table (gwhandle *gwdata, double *table);
double *zr4dwpn_build_biglit_table (gwhandle *gwdata, double *table);
double *zr4dwpn_build_fudge_table (gwhandle *gwdata, double *table);
double *zr4dwpn_build_norm_table (gwhandle *gwdata, double *table);
double *zr4_build_pass2_complex_table (gwhandle *gwdata, double *table);
double *zr4_build_pass2_real_table (gwhandle *gwdata, double *table);

double *yr4_build_onepass_sincos_table (gwhandle *gwdata, double *table);
double *yr4_build_onepass_biglit_table (gwhandle *gwdata, double *table);
double *yr4_build_onepass_norm_table (gwhandle *gwdata, double *table);
double *yr4dwpn_build_pass1_table (gwhandle *gwdata, double *table);
double *yr4dwpn_build_norm_table (gwhandle *gwdata, double *table);
double *yr4dwpn_build_biglit_table (gwhandle *gwdata, double *table);
double *yr4dwpn_build_fixed_pass1_table (gwhandle *gwdata, double *table);
double *yr4_build_pass2_complex_table (gwhandle *gwdata, double *table);
double *yr4_build_pass2_real_table (gwhandle *gwdata, double *table);

double *r4_build_pass1_table (gwhandle *gwdata, double *table);
double *r4_build_pass2_complex_table (gwhandle *gwdata, double *table);
double *r4_build_pass2_real_table (gwhandle *gwdata, double *table);
double *r4delay_build_fixed_premult_table (gwhandle *gwdata, double *table);
double *r4delay_build_pass1_table (gwhandle *gwdata, double *table);
double *r4delay_build_fixed_pass1_table (gwhandle *gwdata, double *table);
double *r4dwpn_build_pass1_table (gwhandle *gwdata, double *table);
double *r4_build_norm_table (gwhandle *gwdata, double *table, int col);
double *r4dwpn_build_norm_table (gwhandle *gwdata, double *table);
double *r4_build_biglit_table (gwhandle *gwdata, double *table);
double *r4dwpn_build_biglit_table (gwhandle *gwdata, double *table);
double *hg_build_sin_cos_table (double	*table, unsigned long N, int hermetian_skip, int type);
double *hg_build_premult_table (gwhandle *gwdata, double *table);
double *hg_build_plus1_table (gwhandle *gwdata, double *table);
double *hg_build_norm_table (gwhandle *gwdata, double *table, int col);
double *hg_build_biglit_table (gwhandle *gwdata, double *table);

double *x87_build_sin_cos_table (double	*table, unsigned long N, int hermetian_skip);
double *x87_build_premult_table (gwhandle *gwdata, double *table);
double *x87_build_plus1_table (gwhandle *gwdata, double *table);
double *x87_build_norm_table (gwhandle *gwdata, double *table, int col);
double *x87_build_biglit_table (gwhandle *gwdata, double *table);

#ifdef __cplusplus
}
#endif

#endif
