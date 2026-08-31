/*----------------------------------------------------------------------
| polymult.c
|
| This file contains the C routines for fast polynomial multiplication
| 
|  Copyright 2021-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

/* Include files */

#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include "cpuid.h"
#include "gwnum.h"
#include "gwutil.h"
#include "polymult.h"
#if defined (SSE2) || defined (AVX) || defined (FMA) || defined (AVX512)
#include <immintrin.h>
#endif

// TO DO OR CONSIDER:
// 1) Support non-power-of-two FFTs below 32?  Support FFT size requiring a radix-2 step?
// 9) Is a fast convert-gwnum-to-larger-FFT feasible? necessary?
// 10) Tune for Brute / Karatsuba / FFT crossovers.
// 12) Tune routine that returns needed safety margin needed for given poly sizes
// 16) Handle out-of-memory gracefully
// 17) Right collection of PFA FFT sizes?
// 22) Can we add asserts that polymult isn't violating EXTRA_BITS?
// 25) Optimize.  Read/write several cache lines at a time (fill up L2 cache?).  Prefetch future cache lines.
// 28) Optimize brute_force to do less compute when one or both invec entries are RLPs.
// 38)  For poly #2, is there a quick block compression we can do where the extra read/write cost will be more than compensated by the few more extra
//	gwnums we can add to poly #2?
// 39)  Think more on unbalanced poly mults.  Is a 100 x 200 mult better done as two 100 x 100 multiplies?  Answer: Never for brute force, maybe for FFT.
//	If we can save the smaller RLP in FFTed state using smaller FFT size that might influence our decision.
// 41) Add callback routine to check for ^C/ESC
// 42) Transparent / or easy way to store gwnums on disk as part of input and/or output vectors.  Picture using 12GB memory and 50GB disk for really huge polys.
// 43) Read_line/write_line that reads/writes data with stride!=1?
// 45) If twiddle_stride gets large, build a smaller twiddle table for smaller strides.
// 48) Test if hyperthreading is beneficial
// 52) Half-sized DCT for RLPs?  The DJB FFTs may make reordering DCT outputs for an inverse FFT tricky.
// 53) Can P-1's circular monic-poly times monic-RLP be safely done without monic_adjustments allowing us to save the FFT'ed RLP that is half-sized due to DCT?
// 54) Does monolithic two-forward-FFTs-pointmul-and-one-inverse-FFT routine suggest different preferred pass sizes (use half the L2 cache)?
// 56) Option to not mul by 1/FFTLEN?
// 57) Option to save memory by processing AVX-512 data using FMA3.  (reduces sizeof(CVDT)).  Not needed now that read_line is multi-threaded.
// 61) Can loop unrolling help in radix-3/4/5 sections?  Seems unlikely.
// 62) Linux stage 2 timings vary widely from 6.7 to 7.0 sec.  What explains the 5% slower runs?  4KB/64KB alignments?
// 64) Save memory in polymult_line.  For large FFTs with big buffer requirements, offer to re-read invec1 and/or invec2 for post-monic addin (much the same
//	way FMAvec is read in without requiring a buffer).  This is awkward if the vector being re-read has been pre-processed.
// 65) An fmavec of NULL could mean all zeros.  Combined with FNMADD would turn into negate outputs.  Code handles this, ASSERTS do not.
// 66) Offer polymult option where caller guarantees that invec1 and/or invec2 have all been FFTed??  Too minor an optimization?
// 67) Offer polymult option where plan is saved??  Too minor an optimization?
// 69) When compressing with POLYMULT_PRE_COMPRESS, figure out a way to malloc a smaller buffer and grow if necessary rather than malloc a large one and
//	shrink at end.
// 70) All atomic_set to zero calls are safe (helpers not launched yet).  Can we offer a faster atomic_set_guaranteed_ok?  Too minor an optimization?
// 71) We're not using AVX-512 code on really small gwnums because gwsetup clears the AVX-512 flag.  Convert AVX data to AVX-512 or write AVX-512 gwnum FFTs below 1K.
// 72) Take a 3.2M AVX FFT.  8 doubles * 3.2M = 205MB.  Read_line into invec1 is a read-from-memory / write-to-memory operation.
//     Instead make fft_line call read_line_slice in pass 1.  This reads directly into AVX registers then writes to scratch.   Saves a r/w.
//     Similarly fft_line could call write_line_slice in pass 3 (perhaps doing monic adjustments along the way).
//     Preprocess_line might could group pass 1 data together for better cache locality and hardware prefetching.  This implies preproces line knows
//	the fft_size and pass1_size.
// 73) Have fft_line use swizzling to reduce memory requirements.  If that changes fft from read/writing memory to using L3 cache it could be faster (as well
//     as using lots less scratch memory)
// 74) Fix mem_required to access L3_CACHE_SIZE and use same algorithm for determining mt_polymult_line.
// 75) If poly is pre-FFT'ed there is no need to read it into a line buffer, we can access it directly in fft_line_pass (or if compressed copy ir to a much smaller scratch buffer)
// 78) Are there any cases where fused-multiply-add of polys makes sense?  That is, fft_line takes 3 args and computes a * b + c.  Presently FMA is done after the
//     FFT so as to support brute/Karatsuba.  Perhaps monics complicate things too - and avoiding FFTing the FMA addin vector could well be faster.
// 79) Pick_pass_sizes only puts powers of two in pass 2.  Does this lead to nasty 4KB strides that we could avoid?  Add padding, maybe only in scratch area?
// 80) put read-specific and write-specific data within a union in read_write_line_slice_data
// 81) 2 levels of radix-8 is cheaper than 3 levels of radix-4.  If radix-8 is cheaper than radix-4, perhaps a radix-6 would let us use a faster radix-8 than 2 radix-4s.
// 82) I don't like the way num_compressed_blks is chosen (only powers of two).  If not pre-FFTing we might get very small blk_size.  Change compress and decompress
//	to handle partial final blk.  blk_sizes were added for multi-threading the preprocessing.  Address this when we tackle strided_reads.
// 83) Under what circumstances do streaming stores provide a benefit?
// 84) A more flexible threading policy.  Now we only support one line at a time using all threads OR each thread doing its own line.  For FFT size 64 where
//	num_lines=9 if threads=14 then 5 threads are totally unused.  Better might be processing 3 lines at a time using 5,5,4 threads.  This is hard as we'd
//	need locks and mutexes for each of the 3 lines being processed.  Easier might be 8 threads processing a line with a single thread and one line being
//	processed with 6 threads.  When that line completes the 6 threads switch to help processing one of the other 8 lines, and so on.

/* Opcodes for helper thread */

#define HELPER_PREPROCESS_LINE		1
#define HELPER_PRE_FFT			2
#define HELPER_POLYMULT_LINE		3
#define HELPER_POST_UNFFT		4
#define HELPER_EXIT			5

// Forward declarations required for the SSE2, AVX, FMA, AVX512 compiles
void pick_pass_sizes (pmhandle *pmdata, uint64_t fftsize, uint32_t *p1size, uint32_t *p2size);
unsigned long cache_line_offset (gwhandle *gwdata, unsigned long i);
void generate_sincos (pmhandle *pmdata, uint64_t);
void zpad7_fft (gwhandle *gwdata, gwnum g);
uint64_t compress_line (char *buf, uint64_t size, int num_compressed_blks);
void decompress_line_slice (char *inbuf, char *outbuf, 	preprocessed_poly_header *hdr, uint64_t slice_start, uint64_t slice_end, int cvdt_size);
void polymult_pre_fft (pmhandle *pmdata, gwhandle *);
void polymult_post_unfft (pmhandle *pmdata, gwhandle *);
void polymult_line_preprocess_dbl (pmhandle *pmdata);
void polymult_line_preprocess_sse2 (pmhandle *pmdata);
void polymult_line_preprocess_avx (pmhandle *pmdata);
void polymult_line_preprocess_fma (pmhandle *pmdata);
void polymult_line_preprocess_avx512 (pmhandle *pmdata);
void polymult_line_dbl (pmhandle *pmdata);
void polymult_line_sse2 (pmhandle *pmdata);
void polymult_line_avx (pmhandle *pmdata);
void polymult_line_fma (pmhandle *pmdata);
void polymult_line_avx512 (pmhandle *pmdata);

// Internal description of the plan to preprocess a poly
typedef struct {
	bool	strip_monic_from_invec1;// True if ones are stripped from monic invec1 requiring (99% of the time) invec2 to be added in during post processing
	int	impl;			// 0 = brute force, 1 = Karatsuba, 2 = poly FFT
	uint64_t fft_size;		// FFT size for impl type 2
	uint64_t adjusted_invec1_size;	// Sizes of the possibly smaller partial polymult result prior to post-monic-adjustment creating the full result
	uint64_t adjusted_invec2_size;
	uint64_t adjusted_outvec_size;
	uint64_t true_invec1_size;	// Sizes of the full polymult inputs and outputs
	uint64_t true_invec2_size;
	uint64_t true_outvec_size;
	uint64_t max_element_size;	// When compressing, helpers return the max_element_size
	bool	combine_two_lines;	// TRUE if we're saving memory by delaying splitting the two reals, the first two lines are combined into one
	preprocessed_poly_header *hdr;  // Header of the preprocessed poly under construction
} preprocess_plan;

// Internal description of the plan to multiply two or more polys
typedef struct {
	bool	mt_polymult_line;	// TRUE if polymult_line is multi-threaded and read_line, fft_line_pass, write_line are single-threaded
	uint64_t alloc_invec1_size;	// Pre-calculated allocation size for invec1
	uint64_t alloc_invec2_size;	// Pre-calculated allocation size for invec2
	uint64_t alloc_outvec_size;	// Pre-calculated allocation size for outvec
	uint64_t alloc_tmpvec_size;	// Pre-calculated allocation size for tmpvec
	float	unnorms1;		// Unnormalized add count of top invec1 coefficient
#ifdef GDEBUG
	uint64_t hash;
#endif
	struct planpart {
		bool	emulate_circular;	// Emulating circular_size is required
		bool	strip_monic_from_invec1;// True if ones are stripped from monic invec1 requiring (99% of the time) invec2 to be added during post processing
		bool	strip_monic_from_invec2;// True if ones are stripped from monic invec2 requiring (99% of the time) invec1 to be added during post processing
		bool	post_monic_addin_invec1;// True if ones are stripped from monic invec2 requiring invec1 to be added during post processing
		bool	post_monic_addin_invec2;// True if ones are stripped from monic invec1 requiring invec2 to be added during post processing
		bool	streamed_stores;	// Use streamed stores
		int	impl;			// 0 = brute force, 1 = Karatsuba, 2 = poly FFT
		uint64_t fft_size;		// FFT size for impl type 2
		uint64_t adjusted_invec1_size;	// Sizes of the possibly smaller partial polymult result prior to post-monic-adjustment creating the full result
		uint64_t adjusted_invec2_size;
		uint64_t adjusted_outvec_size;
		uint64_t true_invec1_size;	// Sizes of the full polymult inputs and outputs
		uint64_t true_invec2_size;
		uint64_t true_outvec_size;
		uint64_t circular_size;		// Return result modulo (X^circular_size - 1)
		uint64_t LSWs_skipped;		// Least significant coefficients that do not need to be returned
		uint64_t MSWs_skipped;		// Most significant coefficients (of true_outvec_size) that do not need to be returned
		int64_t addin[4];		// Outvec locations where four 1*1 values may need to be added at the very end of the polymult process
		int64_t subout;			// Outvec location where 1*1 value may need to be subtracted at the very end of the polymult process
		int	adjusted_shift;		// Number of coefficients to shift left the initial partial poly multiplication to reach true_outvec_size
		int	adjusted_pad;		// Number of coefficients to pad on the left of the initial partial poly multiplication to reach true_outvec_size
		float	unnorms2;		// Unnormalized add count of top invec2 coefficient
	} planpart[1];
} polymult_plan;

#define POLYMULT_IMPL_BRUTE		0		// Brute force polymult
#define POLYMULT_IMPL_KARATSUBA		1		// Karasuba polymult
#define POLYMULT_IMPL_FFT		2		// FFT based polymult

#define PLAN_OPTIONS	(POLYMULT_SAVE_PLAN | POLYMULT_USE_PLAN)					// Options that govern all of the poly multiplications
#define INVEC1_OPTIONS	(POLYMULT_INVEC1_MONIC | POLYMULT_INVEC1_RLP | POLYMULT_INVEC1_NEGATE)		// Options specific to invec1
#define INVEC2_OPTIONS	(POLYMULT_INVEC2_MONIC | POLYMULT_INVEC2_RLP | POLYMULT_INVEC2_NEGATE)		// Options specific to invec2
#define OUTVEC_OPTIONS	(~(PLAN_OPTIONS | INVEC1_OPTIONS | INVEC2_OPTIONS))				// Options that govern each poly multiplication

// Handy vector size macros
#define vector_size_in_doubles(f)		(((f) & CPU_AVX512F) ? 8 : ((f) & CPU_AVX) ? 4 :((f) & CPU_SSE2) ? 2 : 1)
#define vector_size_in_bytes(f)			(((f) & CPU_AVX512F) ? 8*8 : ((f) & CPU_AVX) ? 4*8 :((f) & CPU_SSE2) ? 2*8 : 1*8)
#define complex_vector_size_in_doubles(f)	(((f) & CPU_AVX512F) ? 8*2 : ((f) & CPU_AVX) ? 4*2 :((f) & CPU_SSE2) ? 2*2 : 1*2)
#define complex_vector_size_in_bytes(f)		(((f) & CPU_AVX512F) ? 8*2*8 : ((f) & CPU_AVX) ? 4*2*8 :((f) & CPU_SSE2) ? 2*2*8 : 1*2*8)

// SIMD-specific code

#if defined (AVX512)
#define VLEN			512			// Vector is 256 bits wide
#define VDT			__m512d			// Vector data type
#define VFMA			1			// FMA is supported
#define	prep_line		prep_line_avx512	// Naming extension for this option
#define	read_line		read_line_avx512
#define	read_line_slice		read_line_slice_avx512
#define	read_preprocess_line	read_preprocess_line_avx512
#define	read_preprocess_line_slice read_preprocess_line_slice_avx512
#define	write_line		write_line_avx512
#define	write_line_slice	write_line_slice_avx512
#define	write_line_slice_strided write_line_slice_strided_avx512
#define	brute_line		brute_line_avx512
#define	karatsuba_line		karatsuba_line_avx512
#define	fft_line		fft_line_avx512
#define	fft_line_pass		fft_line_pass_avx512
#define	monic_line_add_hi	monic_line_add_hi_avx512
#define	monic_line_add_lo	monic_line_add_lo_avx512
#define	monic_line_adjustments	monic_line_adjustments_avx512
#define	polymult_line_preprocess polymult_line_preprocess_avx512
#define	polymult_line		polymult_line_avx512
#define broadcastsd		_mm512_set1_pd
#define addpd			_mm512_add_pd
#define subpd			_mm512_sub_pd
#define mulpd			_mm512_mul_pd
#define divpd			_mm512_div_pd
#define fmaddpd			_mm512_fmadd_pd
#define fmsubpd			_mm512_fmsub_pd
#define fnmaddpd		_mm512_fnmadd_pd
#define streampd(a,b)		_mm512_stream_pd((double*)(a),b)
#elif defined (FMA)
#define VLEN			256			// Vector is 256 bits wide
#define VDT			__m256d			// Vector data type
#define VFMA			1			// FMA is supported
#define	prep_line		prep_line_avx		// Naming extension for this option
#define	read_line		read_line_avx
#define	read_line_slice		read_line_slice_avx
#define	read_preprocess_line	read_preprocess_line_avx
#define	read_preprocess_line_slice read_preprocess_line_slice_avx
#define	write_line		write_line_avx
#define	write_line_slice	write_line_slice_avx
#define	write_line_slice_strided write_line_slice_strided_avx
#define	brute_line		brute_line_fma
#define	karatsuba_line		karatsuba_line_fma
#define	fft_line		fft_line_fma
#define	fft_line_pass		fft_line_pass_fma
#define	monic_line_add_hi	monic_line_add_hi_fma
#define	monic_line_add_lo	monic_line_add_lo_fma
#define	monic_line_adjustments	monic_line_adjustments_fma
#define	polymult_line_preprocess polymult_line_preprocess_fma
#define	polymult_line		polymult_line_fma
#define broadcastsd		_mm256_set1_pd
#define addpd			_mm256_add_pd
#define subpd			_mm256_sub_pd
#define mulpd			_mm256_mul_pd
#define divpd			_mm256_div_pd
#define fmaddpd			_mm256_fmadd_pd
#define fmsubpd			_mm256_fmsub_pd
#define fnmaddpd		_mm256_fnmadd_pd
#define streampd(a,b)		_mm256_stream_pd((double*)(a),b)
#elif defined (AVX)
#define VLEN			256			// Vector is 256 bits wide
#define VDT			__m256d			// Vector data type
#define VFMA			0			// FMA not supported
#define	prep_line		prep_line_avx		// Naming extension for this option
#define	read_line		read_line_avx
#define	read_line_slice		read_line_slice_avx
#define	read_preprocess_line	read_preprocess_line_avx
#define	read_preprocess_slice_line read_preprocess_line_slice_avx
#define	write_line		write_line_avx
#define	write_line_slice	write_line_slice_avx
#define	write_line_slice_strided write_line_slice_strided_avx
#define	brute_line		brute_line_avx
#define	karatsuba_line		karatsuba_line_avx
#define	fft_line		fft_line_avx
#define	fft_line_pass		fft_line_pass_avx
#define	monic_line_add_hi	monic_line_add_hi_avx
#define	monic_line_add_lo	monic_line_add_lo_avx
#define	monic_line_adjustments	monic_line_adjustments_avx
#define	polymult_line_preprocess polymult_line_preprocess_avx
#define	polymult_line		polymult_line_avx
#define broadcastsd		_mm256_set1_pd
#define addpd			_mm256_add_pd
#define subpd			_mm256_sub_pd
#define mulpd			_mm256_mul_pd
#define divpd			_mm256_div_pd
#define streampd(a,b)		_mm256_stream_pd((double*)(a),b)
#elif defined (SSE2)
#define VLEN			128			// Vector is 128 bits wide
#define VDT			__m128d			// Vector data type
#define VFMA			0			// FMA not supported
#define	prep_line		prep_line_sse2		// Naming extension for this option
#define	read_line		read_line_sse2
#define	read_line_slice		read_line_slice_sse2
#define	read_preprocess_line	read_preprocess_line_sse2
#define	read_preprocess_line_slice read_preprocess_line_slice_sse2
#define	write_line		write_line_sse2
#define	write_line_slice	write_line_slice_sse2
#define	write_line_slice_strided write_line_slice_strided_sse2
#define	brute_line		brute_line_sse2
#define	karatsuba_line		karatsuba_line_sse2
#define	fft_line		fft_line_sse2
#define	fft_line_pass		fft_line_pass_sse2
#define	monic_line_add_hi	monic_line_add_hi_sse2
#define	monic_line_add_lo	monic_line_add_lo_sse2
#define	monic_line_adjustments	monic_line_adjustments_sse2
#define	polymult_line_preprocess polymult_line_preprocess_sse2
#define	polymult_line		polymult_line_sse2
#define broadcastsd		_mm_set1_pd
#define addpd			_mm_add_pd
#define subpd			_mm_sub_pd
#define mulpd			_mm_mul_pd
#define divpd			_mm_div_pd
#define streampd(a,b)		_mm_stream_pd((double*)(a),b)
#else
#define VLEN			64			// Vector is 64 bits wide
#define VDT			double			// Vector data type
#define VFMA			0			// FMA not supported
#define	prep_line		prep_line_dbl		// Naming extension for this option
#define	read_line		read_line_dbl
#define	read_line_slice		read_line_slice_dbl
#define	read_preprocess_line_slice read_preprocess_line_slice_dbl
#define	write_line		write_line_dbl
#define	write_line_slice	write_line_slice_dbl
#define	write_line_slice_strided write_line_slice_strided_dbl
#define	brute_line		brute_line_dbl
#define	karatsuba_line		karatsuba_line_dbl
#define	fft_line		fft_line_dbl
#define	fft_line_pass		fft_line_pass_dbl
#define	monic_line_add_hi	monic_line_add_hi_dbl
#define	monic_line_add_lo	monic_line_add_lo_dbl
#define	monic_line_adjustments	monic_line_adjustments_dbl
#define	polymult_line_preprocess polymult_line_preprocess_dbl
#define	polymult_line		polymult_line_dbl
#define broadcastsd		_mm64_set1_pd
#define addpd			_mm64_add_pd
#define subpd			_mm64_sub_pd
#define mulpd			_mm64_mul_pd
#define divpd			_mm64_div_pd
#define streampd		_mm64_stream_pd
// Emulate vector length 1 using Intel-style instrinsic naming conventions
#define __m64d			double
#define _mm64_set1_pd(a)	(a)
#define _mm64_add_pd(a,b)	((a)+(b))
#define _mm64_sub_pd(a,b)	((a)-(b))
#define _mm64_mul_pd(a,b)	((a)*(b))
#define _mm64_div_pd(a,b)	((a)/(b))
#define _mm64_stream_pd(a,b)	(*(double *)(a)=(b))
#endif

// Emulate FMA on CPUs that do not have an FMA instruction
#if !VFMA
#define fmaddpd(a,b,c)	addpd(mulpd(a,b),c)
#define fmsubpd(a,b,c)	subpd(mulpd(a,b),c)
#define fnmaddpd(a,b,c)	subpd(c,mulpd(a,b))
#endif

// Structure for complex VDTs
typedef struct {
	VDT	real;
	VDT	imag;
} CVDT;
#define cvload(a,b,c)		(a).real = broadcastsd (b), (a).imag = broadcastsd (c)
#define cvrmul(a,const_d,c)	(a).real = mulpd (const_d, (c).real), (a).imag = mulpd (const_d, (c).imag)				// a = const_d*c
#define cvadd(a,b,c)		(a).real = addpd ((b).real, (c).real), (a).imag = addpd ((b).imag, (c).imag)				// a = b + c
#define cvsub(a,b,c)		(a).real = subpd ((b).real, (c).real), (a).imag = subpd ((b).imag, (c).imag)				// a = b - c
#define cviadd(a,b,c)		(a).real = subpd ((b).real, (c).imag), (a).imag = addpd ((b).imag, (c).real)				// a = b + i*c
#define cvisub(a,b,c)		(a).real = addpd ((b).real, (c).imag), (a).imag = subpd ((b).imag, (c).real)				// a = b - i*c
#define cvaddfm(a,b,const_d,c)	(a).real = fmaddpd (const_d, (c).real, (b).real), (a).imag = fmaddpd (const_d, (c).imag, (b).imag)	// a = b + const_d*c
#define cvsubfm(a,b,const_d,c)	(a).real = fnmaddpd (const_d, (c).real, (b).real), (a).imag = fnmaddpd (const_d, (c).imag, (b).imag)	// a = b - const_d*c
#define cviaddfm(a,b,const_d,c)	(a).real = fnmaddpd (const_d, (c).imag, (b).real), (a).imag = fmaddpd (const_d, (c).real, (b).imag)	// a = b + i*const_d*c
#define cvisubfm(a,b,const_d,c)	(a).real = fmaddpd (const_d, (c).imag, (b).real), (a).imag = fnmaddpd (const_d, (c).real, (b).imag)	// a = b - i*const_d*c
#define cvmul(a,b,c)		{ VDT __tmpproduct__ = mulpd ((b).imag, (c).imag); \
				  (a).imag = fmaddpd ((b).imag, (c).real, mulpd ((b).real, (c).imag)); \
				  (a).real = fmsubpd ((b).real, (c).real, __tmpproduct__); }						// a = b * c
#define cvconjmul(a,b,c)	(a).real = fmaddpd ((b).real, (c).real, mulpd ((b).imag, (c).imag)), \
				(a).imag = fmsubpd ((b).imag, (c).real, mulpd ((b).real, (c).imag))					// a = b * conjugate(c)
// Butterfly operations
#define cvaddsub(a1,a2,b,c)		cvadd (a1, b, c), cvsub (a2, b, c)					// a1 = b + c, a2 = b - c
#define cvsubadd(a1,a2,b,c)		cvaddsub (a2, a1, b, c)							// a1 = b - c, a2 = b + c
#define cviaddsub(a1,a2,b,c)		cviadd (a1, b, c), cvisub (a2, b, c)					// a1 = b + i*c, a2 = b - i*c
#define cvisubadd(a1,a2,b,c)		cviaddsub (a2, a1, b, c)						// a1 = b - i*c, a2 = b + i*c
#if !VFMA
#define cvaddsubfm(a1,a2,b,const_d,c)	{ CVDT _dc; cvrmul (_dc, const_d, c); cvadd (a1, b, _dc); cvsub (a2, b, _dc); }		// a1,a2 = b +,- const_d*c
#define cviaddsubfm(a1,a2,b,const_d,c)	{ CVDT _dc; cvrmul (_dc, const_d, c); cviadd (a1, b, _dc); cvisub (a2, b, _dc); }	// a1,a2 = b +,- i*const_d*c
#else
#define cvaddsubfm(a1,a2,b,const_d,c)	cvaddfm (a1, b, const_d, c), cvsubfm (a2, b, const_d, c)		// a1 = b + const_d*c, a2 = b - const_d*c
#define cviaddsubfm(a1,a2,b,const_d,c)	cviaddfm (a1, b, const_d, c), cvisubfm (a2, b, const_d, c)		// a1 = b + i*const_d*c, a2 = b - i*const_d*c
#endif
#define cvsubaddfm(a1,a2,b,const_d,c)	cvaddsubfm (a2,a1,b,const_d,c)						// a1 = b - const_d*c, a2 = b + const_d*c
#define cvisubaddfm(a1,a2,b,const_d,c)	cviaddsubfm (a2,a1,b,const_d,c)						// a1 = b - i*const_d*c, a2 = b + i*const_d*c

// Structure for twiddle vector data type.
// Twiddles aren't stored as real & imag.  Instead they are stored as cos/sin & sin (a.k.a. real/imag & imag).  This provides more FMA opportunities.
typedef struct {
	VDT	cos_over_sin;
	VDT	sin;
} TVDT;
#define twidload(t,b)			(t).cos_over_sin = broadcastsd ((b)[0]), (t).sin = broadcastsd ((b)[1])
#define twidmul(a,b,t)			(a).real = mulpd (fmsubpd ((b).real, (t).cos_over_sin, (b).imag), (t).sin), \
					(a).imag = mulpd (fmaddpd ((b).imag, (t).cos_over_sin, (b).real), (t).sin)	// a = b * t
#define twidconjmul(a,b,t)		(a).real = mulpd (fmaddpd ((b).real, (t).cos_over_sin, (b).imag), (t).sin), \
					(a).imag = mulpd (fmsubpd ((b).imag, (t).cos_over_sin, (b).real), (t).sin)	// a = b * conjugate(t)
#define twidmulandconjmul(d1,d2,b,t)	{ CVDT bsin; bsin.real = mulpd ((b).real, (t).sin), bsin.imag = mulpd ((b).imag, (t).sin), \
					  (d1).real = fmsubpd (bsin.real, (t).cos_over_sin, bsin.imag), \
					  (d1).imag = fmaddpd (bsin.imag, (t).cos_over_sin, bsin.real), \
					  (d2).real = fmaddpd (bsin.real, (t).cos_over_sin, bsin.imag), \
					  (d2).imag = fmsubpd (bsin.imag, (t).cos_over_sin, bsin.real); }		// d1 = b * t, d2 = b * conjugate(t)
#define twidmuldelay(a,b,t)		(a).real = fmsubpd ((b).real, (t).cos_over_sin, (b).imag), \
					(a).imag = fmaddpd ((b).imag, (t).cos_over_sin, (b).real)			// a = b * t, delay mul by sin
#define twidconjmuldelay(a,b,t)		(a).real = fmaddpd ((b).real, (t).cos_over_sin, (b).imag), \
					(a).imag = fmsubpd ((b).imag, (t).cos_over_sin, (b).real)			// a = b * conjugate(t), delay mul by sin

#if !defined (SSE2) && !defined (AVX) && !defined (FMA) && !defined (AVX512)

// Get the needed safety_margin required for an invec1_size by invec2_size polymult
float polymult_safety_margin (uint64_t invec1_size, uint64_t invec2_size) {
	// The smaller poly determines the number of partial products that are added together
	uint64_t n = (invec1_size < invec2_size) ? invec1_size : invec2_size;
	// The larger poly approximates the FFT size of the polymult (assumes POLYMULT_CIRCULAR)
	uint64_t fft_size = (invec1_size > invec2_size) ? invec1_size : invec2_size;
	// According to roe_gwnum.cpp, the number of EXTRA_BITS (output FFT bits) consumed by a polymult ~= log2(n)/2.  However, gwinit's safety margin
	// parameter refers to input FFT bits.  Thus, divide by another 2.  However, in practice the formula fails for large polymults.  P-1 with AVX-512 FFT
	// size 4608, and an RLP poly1 of size 320000 times poly2 of size 1.3M we get roundoff errors.  My theory is that while roe_gwnum.cpp accurately estimates
	// the number of most significant bits needed to safely add 640000 partial products, it is not accurately estimating the rounding errors introduced
	// to the least significant bits computing a 1.3M size FFT in fft_line.  Until we get more data points (the example got a 0.48+ roundoff where 0.4 should
	// be the target maximum acceptable error -- that's log2(0.49/0.4)=0.3 extra output bits), adjust the needed expected bits by 0.3*log2(FFTsize)/log2(1.3M).
	// 2022-02-05: AVX-512 FFT size 4608 on M76333 fails with poly1-size:322560, poly2-size:1339033.  Increasing to 0.4.
	// 2022-03-02: FMA FFT size 640 on M10303 fails with poly1-size:9123840, poly2-size:37280030, roundoff: 0.4453.  Increasing to 0.45.
	return ((float) (log2 ((double) n) / 2.0 + 0.45 * log2 ((double) fft_size) / log2 (1300000.0)) / 2.0f);
}

// Get the FFT size that will be used for an n = invec1_size + invec2_size polymult
uint64_t polymult_fft_size (uint64_t n)
{
//GW: These FFT sizes haven't been timed.  Some may be slower than a larger FFT or more radix-3/5 options might be profitable
	int	pfas[] = {40, 45, 48, 50, 54, 60, 64, 72, 75};		// Provide a variety of radix-3 and radix-5 FFT sizes

	// Power of two when n is small -- brute force is probably faster anyway
	if (n <= 32) for (int i = 4; ; i *= 2) if (i >= n) return (i);

	// Increase n by a power of two until we find the smallest fft size larger than n
	for (uint64_t two_multiplier = 1; ; two_multiplier *= 2) {
		if (n > two_multiplier * 75) continue;
		for (int i = 0; i < sizeof (pfas) / sizeof (int); i++) {
			uint64_t fft_size = two_multiplier * pfas[i];
			if (fft_size >= n && (fft_size % 4 == 0 || fft_size % 2 == 1)) return (fft_size);  // Weed out fft sizes that would require a radix-2 step
		}
	}
}

// Pick FFT pass sizes
void pick_pass_sizes (
	pmhandle *pmdata,		// Handle for polymult library
	uint64_t fftsize,		// FFT size
	uint32_t *p1size,		// Returned pass 1 size
	uint32_t *p2size)		// Returned pass 2 size
{
	uint32_t powers_of_two;
	double	log_max_p2size, log_fftsize;

	// If the FFT should be done in one pass, choose that option
	if (fftsize <= pmdata->two_pass_start) { *p1size = 1; *p2size = (uint32_t) fftsize; return; }

	// Figure out the power-of-two FFT size will fit pass 2
	log_max_p2size = log2 ((double) pmdata->max_pass2_size);

	// Make pass sizes roughly equal
	log_fftsize = log2 ((double) fftsize);
	*p2size = (uint32_t) log_fftsize / 2;
	if (*p2size > (uint32_t) log_max_p2size) *p2size = (uint32_t) log_max_p2size;

	// Make sure any odd power of two is done in pass 2
	for (powers_of_two = 0; (fftsize & (1ULL << powers_of_two)) == 0; powers_of_two++);
	if (*p2size > powers_of_two) *p2size = powers_of_two;
	if ((powers_of_two & 1) == 0) {		// no radix-8 step
		if ((*p2size & 1) == 1) *p2size -= 1;
	} else {
		if ((*p2size & 1) == 0) *p2size -= 1;
	}
	*p1size = (uint32_t) (fftsize >> *p2size);
	*p2size = 1 << *p2size;
}

// Get the memory (in bytes) required for an FFT based polymult.  Use the information to ensure over-allocating memory does not occur.
uint64_t polymult_mem_required (
	pmhandle *pmdata,		// Handle for polymult library
	uint64_t invec1_size,		// Size of poly #1
	uint64_t invec2_size,		// Size of poly #2
	int	options)		// Polymult options
{
	uint64_t adjusted_invec1_size, adjusted_invec2_size, adjusted_outvec_size, fft_size, memory;
	uint32_t pass1_size, pass2_size;
	bool	post_process_monics;

	ASSERTG (pmdata->num_threads >= 1);

	// Determine the complex vector size in bytes
	int complex_vector_size = complex_vector_size_in_doubles (pmdata->gwdata->cpu_flags);

	// Adjust sizes due to RLPs.  Calculate the output size assuming no monic inputs.
	adjusted_invec1_size = (options & POLYMULT_INVEC1_RLP) ? 2 * invec1_size - 1 : invec1_size;
	adjusted_invec2_size = (options & POLYMULT_INVEC2_RLP) ? 2 * invec2_size - 1 : invec2_size;
	adjusted_outvec_size = adjusted_invec1_size + adjusted_invec2_size - 1;
	if (options & POLYMULT_CIRCULAR) {
		if (options & POLYMULT_INVEC1_MONIC) adjusted_invec1_size += (options & POLYMULT_INVEC1_RLP) ? 2 : 1;
		if (options & POLYMULT_INVEC2_MONIC) adjusted_invec2_size += (options & POLYMULT_INVEC2_RLP) ? 2 : 1;
		// Assume circular size will be the larger of the input sizes rounded up to the next poly FFT size
		adjusted_outvec_size = (adjusted_invec1_size > adjusted_invec2_size ? adjusted_invec1_size : adjusted_invec2_size);
		post_process_monics = FALSE;
	} else
		post_process_monics = TRUE;

	// Compute FFT size and memory usage
	fft_size = polymult_fft_size (adjusted_outvec_size);
	pick_pass_sizes (pmdata, fft_size, &pass1_size, &pass2_size);
	memory = fft_size * complex_vector_size;		// FFT of invec1
	memory += (fft_size + 2) * complex_vector_size;		// FFT of invec2/outvec
	if (post_process_monics && (options & (POLYMULT_INVEC2_MONIC | POLYMULT_INVEC1_MONIC))) memory += fft_size * complex_vector_size;

	// Account for scratch area size.  More is needed when all threads are working on a single line.
	if (pass1_size > 1)
		memory += (uint64_t) pass1_size * complex_vector_size * (fft_size >= pmdata->mt_ffts_start && fft_size < pmdata->mt_ffts_end ? pmdata->num_threads : 1);
	
	// Each thread requires memory when multithreading by lines
	memory *= (fft_size < pmdata->mt_ffts_start || fft_size >= pmdata->mt_ffts_end) ? pmdata->num_threads : 1;

	// Also account for sin/cos tables
	return (memory + (fft_size % 3 == 0 ? fft_size * 2 / 3 : fft_size) * sizeof (double));
}

// Initialize a polymult handle
void polymult_init (
	pmhandle *pmdata,		// Handle for polymult library that needs initializing
	gwhandle *gwdata)		// Handle for gwnum FFT library
{
	memset (pmdata, 0, sizeof (pmhandle));
	pmdata->gwdata = gwdata;

	// Initialize default number of threads
	pmdata->max_num_threads = gwget_num_threads (gwdata);
	pmdata->num_threads = pmdata->max_num_threads;

	// Calculate doubles in a complex vector
	int complex_vector_size = complex_vector_size_in_doubles (gwdata->cpu_flags);

	// Calculate how many "lines" or "work units" polymult threads will work on
	int num_doubles = gwfftlen (pmdata->gwdata);					// Number of doubles in a gwnum
	if (pmdata->gwdata->GENERAL_MMGW_MOD) num_doubles *= 2;				// MMGW has a cyclic and negacyclic FFT to work on
	if (!pmdata->gwdata->NEGACYCLIC_FFT) num_doubles += 2;				// Two FFT reals are converted to two complex values
	if (pmdata->gwdata->ZERO_PADDED_FFT) num_doubles += 14;				// Seven real ZPAD values treated as seven complex values
	pmdata->num_lines = divide_rounding_up (num_doubles, complex_vector_size);	// Number of doubles / doubles in a complex vector

	// Initialize default breakeven points
	if (pmdata->gwdata->cpu_flags & CPU_AVX) {
		pmdata->KARAT_BREAK = 32;			//GW: Fix me
		pmdata->FFT_BREAK = 64;				//GW: Fix me
	}
	else {
		pmdata->KARAT_BREAK = 32;			//GW: Fix me
		pmdata->FFT_BREAK = 64;				//GW: Fix me
	}

	// Default enables caching twiddles
	pmdata->cached_twiddles_enabled = TRUE;
	pmdata->twiddle_cache_additions_disabled = FALSE;

	// Set default tuning parameters (L2 = 256KB, L3 = 6MB)
	polymult_default_tuning (pmdata, 256, 6144);

	// Init FFT(1) if needed
	if (!pmdata->gwdata->information_only) {
		if (pmdata->gwdata->GENERAL_MMGW_MOD || pmdata->gwdata->k != 1.0) gwuser_init_FFT1 (pmdata->gwdata);
		if (pmdata->gwdata->ZERO_PADDED_FFT) zpad7_fft (pmdata->gwdata, pmdata->gwdata->GW_FFT1);
	}
}

// Set default polymult tuning using CPU cache sizes.  If this routine is not called, default tuning parameters are set using L2 cache of 256KB, and
// L3 cache of 6144KB (6MB).  These tuning defaults are almost certainly imperfect.  Feel free to change the tuning defaults to suit your exact needs.
// Read the polymult_default_tuning code to see some of the considerations used in selecting tuning parameters.  Also, run prime95 Advanced/Time 8900
// for relevant timings on your CPU.  Beware, these timings can vary considerably from run to run.
void polymult_default_tuning (
	pmhandle *pmdata,		// Handle for polymult library
	uint32_t L2_CACHE_SIZE,		// Optimize FFTs to fit in this size cache (number is in KB).  Default is 256KB.
	uint32_t L3_CACHE_SIZE)		// Optimize FFTs to fit in this size cache (number is in KB).  Default is 6144KB (6MB).
{
	// Calculate doubles in a complex vector
	int complex_vector_size = complex_vector_size_in_doubles (pmdata->gwdata->cpu_flags);

	// Multi-threading lines has an inefficiency if number of threads does not divide number of lines.  For example, if there are 9 lines (gwnum FFT size 64)
	// and eight threads, then 8 threads each do 1 line followed by 1 thread doing a line a 7 threads being idle.
	int lines_we_could_process = divide_rounding_up (pmdata->num_lines, pmdata->num_threads) * pmdata->num_threads;
	double mt_line_efficiency = (double) pmdata->num_lines / (double) lines_we_could_process;

	// Determine the maximum one pass FFT size.  Since there is some overhead associated with two-pass FFTs, we arbitarily start two pass FFTs when
	// FFT data is 4 times the size of the L2 cache.  For 256KB L2 cache with no AVX-512 support, that translates to 262144 * 4 / 64 / 2 = 8192 fft size.
	pmdata->two_pass_start = L2_CACHE_SIZE * 4 * (1024 / complex_vector_size) / 2;

	// Determine the maximum size of pass 2 in two-pass FFTs.  Since we try to make pass 1 size and pass 2 size roughly equal, this setting will only
	// make a difference for FFT sizes larger than max_pass2_size^2.  My thinking here is to find the FFT size that will fit in the L2 cache, so that pass 2
	// is very efficient and only pass 1 pays for cache miss penalties.  This could be wrong, and requires further investigation.  Perhaps three pass
	// FFTs would be a better choice.  Remember an FFT multiplication typically processes two inputs with the output written back to one of the inputs.
	// For 256KB L2 cache with no AVX-512 support, that translates to 262144 / 64 / 2 = 2048 fft size.
	pmdata->max_pass2_size = L2_CACHE_SIZE * (1024 / complex_vector_size) / 2;

	// Guess at a good FFT size to switch from multi-threading lines (each thread does its own FFT on a separate line of data) vs.
	// multi-threading FFTs (all threads cooperating to FFT a single line of data).
	// Benchmarks indicate that multi-threading lines is better as long as all the lines fit in the L3 cache.  We adjust for the inefficiency
	// inherent when multi-threading lines.
	// We adjust this down further (arbitrarily using 0.8) because polymult usually involves reading two lines and writing one.
	pmdata->mt_ffts_start = (uint64_t) ((double) L3_CACHE_SIZE * 1024.0 / complex_vector_size / pmdata->num_threads * mt_line_efficiency * 0.8);

	// Once even a single line exceeds the L3 cache, then it may pay to go back to multi-threading lines.  This is because multi-threading FFTs has
	// more locking and syncing overhead than multi-threading lines.  
	pmdata->mt_ffts_end = (mt_line_efficiency > 0.98 ? (uint64_t) L3_CACHE_SIZE * 1024 / complex_vector_size : 0xFFFFFFFFFFFFFFFFULL);

	// This setting we don't have any good theories.  In theory strided writes should always be beneficial.  Benchmarks show that at large poly sizes
	// non-strided writes win by a significant margin.  This seems to happen around the time a line does not fit in the L3 cache.
	pmdata->strided_writes_end = (uint64_t) L3_CACHE_SIZE * 1024 / complex_vector_size;

	// Streamed stores seem to be beneficial if the result of the polymult does not fit in L3 cache.  We don't want to write to the CPU caches since the data
	// will not be used and thus evicted by the time the gwunfft operations are performed.  NOTE: If the caller is using POLYMULT_NO_UNFFT we have no idea
	// when the gwunfft will take place.  In prime95 P-1 and ECM code builds one big poly from many smaller polys with POLYMULT_NO_UNFFT set.  It is not until
	// all smaller polys have been multiplied that the gwunffts happen.  Thus, Prime95 overrides the default streamed_stores_start value to a much smaller fft
	// size.  NOTE:  The calculation below is the same as "how many gwnum coefficients fit in the L3 cache".
	pmdata->streamed_stores_start = L3_CACHE_SIZE * (1024 / complex_vector_size) / pmdata->num_lines;
}

// Terminate use of a polymult handle.  Free up memory.
void polymult_done (
	pmhandle *pmdata)		// Handle for polymult library
{
	if (pmdata->thread_ids != NULL) {
		pmdata->helper_opcode = HELPER_EXIT;
		gwevent_signal (&pmdata->work_to_do);
		atomic_set (pmdata->alt_work_to_do, 1);
		for (int i = 1; i < pmdata->max_num_threads; i++) gwthread_wait_for_exit (&pmdata->thread_ids[i]);
		free (pmdata->thread_ids);
		pmdata->thread_ids = NULL;
		gwmutex_destroy (&pmdata->poly_mutex);
		gwevent_destroy (&pmdata->work_to_do);
		gwevent_destroy (&pmdata->all_helpers_done);
	}
	if (pmdata->cached_twiddles_enabled) {
		for (int i = 0; i < pmdata->cached_twiddles_count; i++) {
			aligned_free (pmdata->cached_twiddles[i].twiddles1);
			aligned_free (pmdata->cached_twiddles[i].twiddles2);
		}
		pmdata->cached_twiddles_count = 0;
	}
	if (pmdata->twiddles_initialized && !pmdata->twiddles_are_from_cache) {
		aligned_free (pmdata->twiddles1);
		aligned_free (pmdata->twiddles2);
	}
	pmdata->twiddles_initialized = 0;
	pmdata->twiddles1 = NULL;
	pmdata->twiddles2 = NULL;
}


//GW -- move to gwnum.c?

/* Get offset to nth "cache line" from an FFTed gwnum.  Where a cache line is 64 bytes. */

unsigned long cache_line_offset (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	unsigned long i)
{
	unsigned long addr, grps;

/* Memory layouts for AVX-512 machines */

	if (gwdata->cpu_flags & CPU_AVX512F) {
		if (gwdata->PASS1_SIZE == 0) {
			addr = i * 64;
			/* Now optionally add 64 pad bytes every 4KB */
			if (gwdata->FOURKBGAPSIZE) addr = addr + (addr / (gwdata->FOURKBGAPSIZE << 6)) * 64;
		}
		else {
			grps = i / (gwdata->PASS2_SIZE / 4);
			addr = i * 64;
			/* Now optionally add bytes for every 4KB page and every pass 2 block */
			addr = addr + (addr >> 12) * gwdata->FOURKBGAPSIZE + grps * gwdata->PASS2GAPSIZE;
		}
	}

/* Memory layouts for AVX machines */

	else if (gwdata->cpu_flags & CPU_AVX) {
		if (gwdata->PASS2_SIZE == 0) {
			addr = i * 64;
			/* Now optionally add 64 pad bytes every 1KB, 2KB or 4KB */
			if (gwdata->FOURKBGAPSIZE) addr = addr + (addr / (gwdata->FOURKBGAPSIZE << 6)) * 64;
		}
		else {
			grps = i / (gwdata->PASS2_SIZE / 4);
			addr = i * 64;
			/* Now add 64 bytes every 4KB and one pass2gapsize for every pass 2 block. */
			addr = addr + (addr >> 12) * gwdata->FOURKBGAPSIZE + grps * gwdata->PASS2GAPSIZE;
		}
	}

/* Memory layouts for SSE2 machines */

	else if (gwdata->cpu_flags & CPU_SSE2) {

/* PFA-style FFTs are a little tricker.  See assembly code for example.	*/
		if (gwdata->PASS2_SIZE == 0) {
			addr = i * 64;
		}

/* and PFA layouts are even funkier.					*/
		else if (gwdata->FFT_TYPE == FFT_TYPE_HOME_GROWN) {
			grps = i / (gwdata->PASS2_SIZE / 2);
			addr = i * 64;
			/* Now add 128 bytes every 8KB and one pass2gapsize for every pass 2 block. */
			addr = addr + (addr >> 13) * 128 + grps * gwdata->PASS2GAPSIZE;
		}
/* Newer traditional radix-4 large FFTs use don't have a special layout for PFA. */
		else {
			grps = i / (gwdata->PASS2_SIZE / 2);
			addr = i * 64;
			/* Now add 128 bytes every 8KB and one pass2gapsize for every pass 2 block. */
			addr = addr + (addr >> 13) * 128 + grps * gwdata->PASS2GAPSIZE;
		}
	}

/* One pass x87 FFTs use a near flat memory model. */

	else if (gwdata->PASS2_SIZE == 0) {
		addr = i * 64;
	}
	else {
		grps = (i * 8) / gwdata->PASS2_SIZE;
		addr = i * 64 + grps * 64;
	}

/* Return the offset */

	return (addr);
}

//
// Process the 7 values in the header of zero-padded FFT gwnums.  These seven values cannot be multiplied pointwise as is required by polymult.
// The assembly code (see zmult7) multiplies the 7 values keeping the high half of the result.  An zero-padded 13-reals FFT of the 7 values allows
// us to do pointwise multiplies.  A zero-padded FFT produces 1 real value and 6 complex values for a total of 13 doubles.  The extra 6 doubles need
// to be stored somewhere in the gwnum header or a pad line in the gwnum.
//

// Roots of unity for radix-13 FFT
const long double w1c = 0.8854560256532098959003755220151;	// cos(2*pi/13*1)
const long double w1s = 0.4647231720437685456560153351331;	// sin(2*pi/13*1)
const long double w2c = 0.56806474673115580251180755912752;	// cos(2*pi/13*2)
const long double w2s = 0.82298386589365639457961742343938;	// sin(2*pi/13*2)
const long double w3c = 0.12053668025532305334906768745254;	// cos(2*pi/13*3)
const long double w3s = 0.99270887409805399280075164949252;	// sin(2*pi/13*3)
const long double w4c = -0.35460488704253562596963789260002;	// cos(2*pi/13*4)
const long double w4s = 0.93501624268541482343978459983783;	// sin(2*pi/13*4)
const long double w5c = -0.74851074817110109863463059970135;	// cos(2*pi/13*5)
const long double w5s = 0.66312265824079520237678549266677;	// sin(2*pi/13*5)
const long double w6c = -0.97094181742605202715698227629379;	// cos(2*pi/13*6)
const long double w6s = 0.23931566428755776714875372626021;	// sin(2*pi/13*6)
const long double w7c = -0.97094181742605202715698227629379;	// cos(2*pi/13*7)
const long double w7s = -0.23931566428755776714875372626021;	// sin(2*pi/13*7)
const long double w8c = -0.74851074817110109863463059970135;	// cos(2*pi/13*8)
const long double w8s = -0.66312265824079520237678549266677;	// sin(2*pi/13*8)
const long double w9c = -0.35460488704253562596963789260002;	// cos(2*pi/13*9)
const long double w9s = -0.93501624268541482343978459983783;	// sin(2*pi/13*9)
const long double w10c = 0.12053668025532305334906768745254;	// cos(2*pi/13*10)
const long double w10s = -0.99270887409805399280075164949252;	// sin(2*pi/13*10)
const long double w11c = 0.56806474673115580251180755912752;	// cos(2*pi/13*11)
const long double w11s = -0.82298386589365639457961742343938;	// sin(2*pi/13*11)
const long double w12c = 0.8854560256532098959003755220151;	// cos(2*pi/13*12)
const long double w12s = -0.4647231720437685456560153351331;	// sin(2*pi/13*12)

// FFT the 7 zpad values
void zpad7_fft (
	gwhandle *gwdata,		// Handle for gwnum library
	gwnum	g)			// gwnum to process the 7 zpad values in the header
{
	double r1, r2, r3, r4, r5, r6, r7;		// The 7 reals from the header (r7=MSW, r1=LSW)

	// Load the zpad doubles from the header
	r1 = g[-11];		// Load input FFT word at halfway - 3
	r2 = g[-10];		// Load input FFT word at halfway - 2
	r3 = g[-9];		// Load input FFT word at halfway - 1
	r4 = g[-8];		// Load input FFT word at halfway + 0
	r5 = g[-7];		// Load input FFT word at halfway + 1
	r6 = g[-6];		// Load input FFT word at halfway + 2
	r7 = g[-5];		// Load input FFT word at halfway + 3

	// Brute force a zero padded 13-point FFT.  Hermetian symetry allows this to be stored in 7 complex values.
	g[-12] = r1 + r2 + r3 + r4 + r5 + r6 + r7;
	g[-13] = r1 + w1c*r2 +  w2c*r3 +  w3c*r4 +  w4c*r5 +  w5c*r6 +  w6c*r7;
	g[-14] = r1 + w2c*r2 +  w4c*r3 +  w6c*r4 +  w8c*r5 + w10c*r6 + w12c*r7;
	g[-15] = r1 + w3c*r2 +  w6c*r3 +  w9c*r4 + w12c*r5 +  w2c*r6 +  w5c*r7;
	g[-16] = r1 + w4c*r2 +  w8c*r3 + w12c*r4 +  w3c*r5 +  w7c*r6 + w11c*r7;
	g[-17] = r1 + w5c*r2 + w10c*r3 +  w2c*r4 +  w7c*r5 + w12c*r6 +  w4c*r7;
	g[-18] = r1 + w6c*r2 + w12c*r3 +  w5c*r4 + w11c*r5 +  w4c*r6 + w10c*r7;
	g[-19] =      w1s*r2 +  w2s*r3 +  w3s*r4 +  w4s*r5 +  w5s*r6 +  w6s*r7;
	g[-20] =      w2s*r2 +  w4s*r3 +  w6s*r4 +  w8s*r5 + w10s*r6 + w12s*r7;
	g[-21] =      w3s*r2 +  w6s*r3 +  w9s*r4 + w12s*r5 +  w2s*r6 +  w5s*r7;
	g[-22] =      w4s*r2 +  w8s*r3 + w12s*r4 +  w3s*r5 +  w7s*r6 + w11s*r7;
	g[-23] =      w5s*r2 + w10s*r3 +  w2s*r4 +  w7s*r5 + w12s*r6 +  w4s*r7;
	g[-24] =      w6s*r2 + w12s*r3 +  w5s*r4 + w11s*r5 +  w4s*r6 + w10s*r7;
}

// Inverse FFT to reconstruct the 7 zpad values
void zpad7_unfft (
	gwhandle *gwdata,		// Handle for gwnum library
	gwnum	g)			// gwnum to process the 7 zpad values in the header
{
const	double INV13 = 0.07692307692307692307692307692308;	// 1/13
	double r1, r2, r3, r4, r5, r6, r7;			// The 7 FFTed reals
	double i2, i3, i4, i5, i6, i7;				// The 7 FFTed imaginaries (first imaginary is zero)
	double o[7];						// The 13 output reals (we don't need first 6 reals)

	// Load FFT of the zpad doubles
	r1 = g[-12];
	r2 = g[-13];
	r3 = g[-14];
	r4 = g[-15];
	r5 = g[-16];
	r6 = g[-17];
	r7 = g[-18];
	i2 = g[-19];
	i3 = g[-20];
	i4 = g[-21];
	i5 = g[-22];
	i6 = g[-23];
	i7 = g[-24];

	// Brute force the padded 13-point inverse FFT discarding the lower 6 values
	o[0] = r1 + 2.0 * ( w6c*r2 + w12c*r3 +  w5c*r4 + w11c*r5 +  w4c*r6 + w10c*r7 +  w6s*i2 + w12s*i3 +  w5s*i4 + w11s*i5 +  w4s*i6 + w10s*i7);
	o[1] = r1 + 2.0 * ( w7c*r2 +  w1c*r3 +  w8c*r4 +  w2c*r5 +  w9c*r6 +  w3c*r7 +  w7s*i2 +  w1s*i3 +  w8s*i4 +  w2s*i5 +  w9s*i6 +  w3s*i7);
	o[2] = r1 + 2.0 * ( w8c*r2 +  w3c*r3 + w11c*r4 +  w6c*r5 +  w1c*r6 +  w9c*r7 +  w8s*i2 +  w3s*i3 + w11s*i4 +  w6s*i5 +  w1s*i6 +  w9s*i7);
	o[3] = r1 + 2.0 * ( w9c*r2 +  w5c*r3 +  w1c*r4 + w10c*r5 +  w6c*r6 +  w2c*r7 +  w9s*i2 +  w5s*i3 +  w1s*i4 + w10s*i5 +  w6s*i6 +  w2s*i7);
	o[4] = r1 + 2.0 * (w10c*r2 +  w7c*r3 +  w4c*r4 +  w1c*r5 + w11c*r6 +  w8c*r7 + w10s*i2 +  w7s*i3 +  w4s*i4 +  w1s*i5 + w11s*i6 +  w8s*i7);
	o[5] = r1 + 2.0 * (w11c*r2 +  w9c*r3 +  w7c*r4 +  w5c*r5 +  w3c*r6 +  w1c*r7 + w11s*i2 +  w9s*i3 +  w7s*i4 +  w5s*i5 +  w3s*i6 +  w1s*i7);
	o[6] = r1 + 2.0 * (w12c*r2 + w11c*r3 + w10c*r4 +  w9c*r5 +  w8c*r6 +  w7c*r7 + w12s*i2 + w11s*i3 + w10s*i4 +  w9s*i5 +  w8s*i6 +  w7s*i7);

	// Store the zpad doubles in the header
	g[-11] = o[0] * INV13;	// Store output FFT word at halfway - 3
	g[-10] = o[1] * INV13;	// Store output FFT word at halfway - 2
	g[-9]  = o[2] * INV13;	// Store output FFT word at halfway - 1
	g[-8]  = o[3] * INV13;	// Store output FFT word at halfway + 0
	g[-7]  = o[4] * INV13;	// Store output FFT word at halfway + 1
	g[-6]  = o[5] * INV13;	// Store output FFT word at halfway + 2
	g[-5]  = o[6] * INV13;	// Store output FFT word at halfway + 3
}

// Helper routines for FFT code

// Generate radix-3/4/5 twiddle factors
void generate_sincos (
	pmhandle *pmdata,		// Handle for polymult library
	uint64_t fft_size)
{
	const long double twopi = 2.0L * 3.1415926535897932384626433L;
#define gen_twid(t,a)	{long double c,s; c=cosl(a); s=sinl(a)+1e-80; (t)[0]=(double)(c/s); (t)[1]=(double)s; }

	// Return quickly if twiddles have already been initialized
	if (pmdata->twiddles_initialized && pmdata->twiddles_initialized == fft_size) return;

	// Make sure only one thread initializes the twiddles
	if (pmdata->num_threads > 1) gwmutex_lock (&pmdata->poly_mutex);

	// If some other thread beat us to the initialization, then return
	if (pmdata->twiddles_initialized && pmdata->twiddles_initialized == fft_size) {
		if (pmdata->num_threads > 1) gwmutex_unlock (&pmdata->poly_mutex);
		return;
	}

//GW: accept fft_size < pmdata->twiddles_initialized (at least for power of 2)??

	// Free the previous twiddles unless they are cached
	if (pmdata->twiddles_initialized && !pmdata->twiddles_are_from_cache) {
		aligned_free (pmdata->twiddles1);
		aligned_free (pmdata->twiddles2);
	}

	// Look for desired twiddles in the cache
	if (pmdata->cached_twiddles_enabled) {
		for (int i = 0; i < pmdata->cached_twiddles_count; i++) {
			if (fft_size == pmdata->cached_twiddles[i].size) {
				pmdata->twiddles1 = pmdata->cached_twiddles[i].twiddles1;
				pmdata->twiddles2 = pmdata->cached_twiddles[i].twiddles2;
				pmdata->twiddles_initialized = fft_size;
				pmdata->twiddles_are_from_cache = TRUE;
				if (pmdata->num_threads > 1) gwmutex_unlock (&pmdata->poly_mutex);
				return;
			}
		}
	}

	// Clear previous state
	pmdata->twiddles_initialized = 0;
	pmdata->twiddles1 = NULL;
	pmdata->twiddles2 = NULL;

	// Generate twiddles for radix-3 butterflies
	uint64_t size = fft_size;
	if (size % 3 == 0) {
		long double twopi_over_size = twopi / (long double) size;
		pmdata->twiddles1 = (double *) aligned_malloc ((size_t) (size/3 * 2 * sizeof (double)), 64);
		for (unsigned int i = 0; i < size/3; i++) {
			long double angle = twopi_over_size * (long double) i;
			gen_twid (&pmdata->twiddles1[2*i], angle);
		}
		while (size % 3 == 0) size /= 3;
	}

	// Generate twiddles for radix-4/5 butterflies
	long double twopi_over_size = twopi / (long double) size;
	pmdata->twiddles2 = (double *) aligned_malloc ((size_t) (size/4 * 2 * 2 * sizeof (double)), 64);
	for (unsigned int i = 0; i < size/4; i++) {
		double long angle = twopi_over_size * (long double) i;
		gen_twid (&pmdata->twiddles2[2*(2*i)], angle);
		gen_twid (&pmdata->twiddles2[2*(2*i+1)], 2.0L*angle);
	}

	// Set size
	pmdata->twiddles_initialized = fft_size;
	pmdata->twiddles_are_from_cache = FALSE;

	// Add twiddles to the caches
	if (pmdata->cached_twiddles_enabled && !pmdata->twiddle_cache_additions_disabled) {
		int i;
		if (pmdata->cached_twiddles_count < 40) i = pmdata->cached_twiddles_count++;
		else {
			i = fft_size % 37;		// Delete random entry
			aligned_free (pmdata->cached_twiddles[i].twiddles1);
			aligned_free (pmdata->cached_twiddles[i].twiddles2);
		}
		pmdata->cached_twiddles[i].size = fft_size;
		pmdata->cached_twiddles[i].twiddles1 = pmdata->twiddles1;
		pmdata->cached_twiddles[i].twiddles2 = pmdata->twiddles2;
		pmdata->twiddles_are_from_cache = TRUE;
	}

	// Unlock and return
	if (pmdata->num_threads > 1) gwmutex_unlock (&pmdata->poly_mutex);
}

// Compress a line of doubles
// The compression algorithm is pretty simple.  It relies on the observation that exponents in the doubles are clustered around a peak, dropping rapidly
// in frequency as you move away from the peak.  The three exponents that constitute the peak are output with 2-bit values.
// Three more exponents are output with 4-bit values (either 1 above the peak and 2 below the peak or 3 below the peak).
// The next exponent below the peak is output with a 5-bit value.  The remaining exponents are output with a 5-bit escape code followed by the 11-bit
// exponent (many 11-bit exponents are "impossible" and we reduce this to a 5-bit escape code followed by a 6-bit exponent).
// To allow multi-threading in decompress_line_slice, if the vector size is large we compress in slices of 512 doubles.
uint64_t compress_line (		// Returns size of the compressed buffer
	char	*buf,			// A line - the array of doubles to compress
	uint64_t size,			// Size of buffer in bytes
	int	num_compressed_blks)	// Each line buffer is sub-divided into this many blocks
{
#define COUNT_BASE_EXPO		960		// Minimum exponent we are prepared to see as most common
#define COUNT_MAX_EXPO		1080		// Maximum exponent we are prepared to see
	short	counts[COUNT_MAX_EXPO-COUNT_BASE_EXPO+1];
	int	max_expo;		// Maximum count-base-adjusted exponent encountered during sampling
	int	peak;			// The highest exponent in the set of three most common exponents (sometimes count-base-adjusted)
	bool	above_peak_flag;	// TRUE if exponent above the peak is compressed

//char *orig_buf = malloc (size);
//memcpy(orig_buf, buf, size);

	// Sample 1000 doubles to find the most common exponents in the doubles
	memset (&counts, 0, sizeof (counts));
	max_expo = 0;
	for (int i = 0; i < size && i < 8000; i += 8) {
		uint64_t expo = * (uint64_t *) (buf + i);
		expo = (expo << 1) >> 53;		// strip off sign bit and mantissa
		if (expo < COUNT_BASE_EXPO) continue;	// don't look for peak in really small exponents, we'll likely treat these numbers as zero later on
		ASSERTG (expo <= COUNT_MAX_EXPO);	// we should never see an exponent this large
		expo -= COUNT_BASE_EXPO;		// apply count-base-adjustment
		counts[expo]++;
		if ((int) expo > max_expo) max_expo = (int) expo;
	}

	// Find the set of three exponents that are most common
	peak = 0;
	for (int i = (max_expo > 18 ? max_expo - 18 : 0); i < max_expo; i++)
		if (peak == 0 || counts[i-2] + counts[i-1] + counts[i] > counts[peak-2] + counts[peak-1] + counts[peak]) peak = i;

	// Determine expo just above the peak will be compressed (or output raw)
	above_peak_flag = (peak < 5 || counts[peak+1] > counts[peak-5]);

	// Output header bits for the line (one byte) -- peak and above_peak_flag
	*buf = ((peak & 0x7F) << 1) + (above_peak_flag & 1);
	peak += COUNT_BASE_EXPO;		// undo count-base-adjustment

	// For larger line sizes (32K doubles or more), output the data in slices of typically 512 doubles so that the preprocessed data can be read multi-threaded
	uint64_t max_compressed_blk_size = 0;
	uint64_t blk_size = size / num_compressed_blks;
	ASSERTG (size % num_compressed_blks == 0);
	for (int blk = 0; blk < num_compressed_blks; blk++) {

		// Init the output queues
		int	outval = 0;		// Compressed exponent and sign bits output in multiples of 8 bits
		int	outlen = 0;		// Number of bits in outval
		unsigned char mbuf[48];		// Mantissa buffer
		int	mlen = 0;		// Number of chars in mbuf
		int	last_m48len = 0;	// Number of chars in the last mantissa48 (either 0 or 6)
#define put_bits(v,len)	outval = (outval << (len)) + (int)(v), outlen += len
#define put_m48(m)	{ union {uint64_t a; char b[8];} x; x.a = m; memcpy (mbuf + mlen, x.b, 6); mlen += 6; last_m48len = 6; }
#define emptyQ()	{ if (outlen>=8) outlen-=8, *outptr++ = (unsigned char)(outval>>outlen); \
			  if (mlen > last_m48len) mlen-=last_m48len, memcpy(outptr,mbuf,mlen), outptr+=mlen, memcpy(mbuf,mbuf+mlen,last_m48len), mlen=last_m48len; \
			  while (outlen>=8) outlen-=8, *outptr++ = (unsigned char)(outval>>outlen); \
			  if (outlen == 0 && mlen) memcpy(outptr,mbuf,mlen), outptr+=mlen, mlen=0; \
			  last_m48len = 0; }
		unsigned char *outptr = (unsigned char *) buf + 1 + blk * blk_size;
		unsigned char *blk_start = outptr;

		// Loop compressing doubles
		for (uint64_t i = 0; i < blk_size; i += sizeof (double)) {
			uint64_t expo, sign, nibble, mantissa48;

			// Parse the double
			expo = * (uint64_t *) (buf + blk * blk_size + i);
			sign = expo >> 63;
			nibble = (expo << 12) >> 60;
			mantissa48 = expo & 0xFFFFFFFFFFFFULL;
			expo = (expo << 1) >> 53;	// strip off sign bit and mantissa

			// Empty the output queues (as much as possible)
			emptyQ ();

			// Rare case of compressing FFTed data that contains lots of zeros.  The FFTed data may contain lots of really small exponent values (noise)
			// that would greatly dimish our compression.  Turn the noise into zeros.
			if (expo < peak - 50) expo = 0;

			// Compress the exponent
			if (expo == peak) put_bits (0, 2);
			else if (expo == peak-1) put_bits (1, 2);
			else if (expo == peak-2) put_bits (2, 2);
			else if (expo == peak-3) put_bits (12, 4);
			else if (expo == peak-4) put_bits (13, 4);
			else if (above_peak_flag && expo == peak+1) put_bits (14, 4);
			else if (above_peak_flag && expo == peak-5) put_bits (30, 5);
			else if (!above_peak_flag && expo == peak-5) put_bits (14, 4);
			else if (!above_peak_flag && expo == peak-6) put_bits (30, 5);
			else {
				put_bits (31, 5);
				ASSERTG (expo == 0 || (expo >= peak-50 && expo <= peak-6) || (expo >= peak+1 && expo <= peak+18));
				if (expo > peak) expo = expo - (peak+1) + 1;		// Handle up to 18 values above peak coded as 1 through 18
				else if (expo > 0) expo = expo - (peak-50) + 19;	// Handle up to 45 values below peak coded as 19 through 63
				ASSERTG (expo <= 63);
				put_bits (expo, 6);
			}

			// Output the sign bit, high nibble of mantissa, and rest of the mantissa
			if (expo != 0) { put_bits (sign, 1); put_bits (nibble, 4); put_m48 (mantissa48); }
		}

		// Empty the output queues
		if (outlen & 7) put_bits (0, 8 - (outlen & 7));
		emptyQ ();

		// Keep track of the largest compressed blk size
		if ((uint64_t) (outptr - blk_start) > max_compressed_blk_size) max_compressed_blk_size = outptr - blk_start;
	}
#undef put_bits
#undef put_m48
#undef emptyQ

//char *xxx = malloc (size);
//decompress_line_slice (buf, xxx, size);
//ASSERTG(memcmp(orig_buf, xxx, size)==0);
//free(xxx);
//free(orig_buf);

	// Return the size of the compressed line
	return (1 + num_compressed_blks * max_compressed_blk_size);
}


// Decompress all of a line or a slice of a line
void decompress_line_slice (
	char	*inbuf,			// Compressed array of doubles
	char	*outbuf,		// Decompressed array of doubles
	preprocessed_poly_header *hdr,  // Header of the preprocessed poly being decompressed
	uint64_t slice_start,		// Starting line slice number
	uint64_t slice_end,		// Ending line slice number
	int	cvdt_size)		// Sizeof(CVDT) in bytes
{
	int	peak;			// The highest exponent in the set of three most common exponents
	bool	above_peak_flag;	// TRUE if exponent above the peak is compressed

	// Read the first byte in a compressed line -- peak exponent and above-peak-flag
	peak = ((unsigned char) *inbuf) >> 1;  peak += 960;
	above_peak_flag = *inbuf & 1;

	// Handle reading compressed data in slices and blocks
	uint64_t doubles_to_skip = slice_start * (cvdt_size / sizeof (double));

	// Loop through compressed blocks until we reach slice)_end
	uint64_t total_size = (uint64_t) (slice_end - slice_start) * cvdt_size;
	uint64_t compressed_blk_size = (hdr->element_size - 1) / hdr->num_compressed_blks;
	uint64_t uncompressed_blk_size = (uint64_t) hdr->line_size * cvdt_size / hdr->num_compressed_blks;
	ASSERTG ((doubles_to_skip * sizeof (double)) % uncompressed_blk_size == 0);	// No support for reading part of a compressed block
	for (uint64_t blk = doubles_to_skip / 512; total_size; blk++) {
		uint64_t size;		// Number of bytes to write to output buffer for this compressed block

		// Init input queue
		unsigned char *inptr = (unsigned char *) inbuf + 1 + blk * compressed_blk_size;
		unsigned int inval, inbits;	// Queue to extract bits from inbuf
		inval = inbits = 0;
#define get_bits(v,len)	{ if (inbits<len) inval=(inval<<8)+(*inptr++),inbits+=8; inbits-=len; v=(inval>>inbits)&((1<<(len))-1); }
#define get_m48(v)	{ union {uint64_t a; char b[8];} x; x.a = 0; memcpy (x.b, inptr, 6); inptr += 6; v = x.a; }

		// Read either one compressed block or the entire amount
		size = uncompressed_blk_size;
		total_size -= size;

#ifdef STRAIGHTFORWARD_IMPL
		while (size) {
			uint64_t expo, sign, nibble, mantissa48;
			int	code;

			// Decompress the exponent
			get_bits (code, 2);
			if (code <= 2) expo = peak - code;
			else {
				get_bits (code, 2);
				if (code <= 1) expo = peak - 3 - code;
				else if (code == 2) expo = above_peak_flag ? peak + 1 : peak - 5;
				else {
					get_bits (code, 1);
					if (code == 0) expo = above_peak_flag ? peak - 5 : peak - 6;
					else {
						get_bits (code, 6);
						if (code >= 19) expo = code - 19 + (peak-50);	// Handle up to 45 values below peak coded as 19 through 63
						else if (code >= 1) expo = code - 1 + (peak+1);	// Handle up to 18 values above peak coded as 1 through 18
						else expo = 0;
					}
				}
			}

			// Get the sign bit and mantissa
			if (expo != 0) { get_bits (sign, 1); get_bits (nibble, 4); get_m48 (mantissa48); }
			else sign = nibble = mantissa48 = 0;

			// Output the uncompressed double, move onto next double
			* (uint64_t *) outbuf = (sign << 63) + (expo << 52) + (nibble << 48) + mantissa48;
			outbuf += sizeof (double);
			size -= sizeof (double);
		}

#else		// This version is less readable but a little faster

		for ( ; size; outbuf += sizeof (double), size -= sizeof (double)) {
			int	code, more_code, expo, sign, nibble;

			// We need to get at least 7 bits, cheaper to get them all at once.
			// The goal is to decompress ~75% of the expos using just one get_bits call.
			get_bits (code, 7);

			// Decompress the exponent
			if (code < 0b1100000) expo = peak - (code >> 5);
			else if (code < 0b1111000) {
				if (code < 0b1110000) expo = peak - 3 - ((code & 0b0011000) >> 3);
				else expo = above_peak_flag ? peak + 1 : peak - 5;
				get_bits (more_code, 2);
				code = (code << 2) + more_code;
			} else if (code < 0b1111100) {
				expo = above_peak_flag ? peak - 5 : peak - 6;
				get_bits (more_code, 3);
				code = (code << 3) + more_code;
			} else {
				get_bits (more_code, 4);
				code = ((code & 0b0000011) << 4) + more_code;
				if (code >= 19) expo = code - 19 + (peak-50);	// Handle up to 45 values below peak coded as 19 through 63
				else if (code >= 1) expo = code - 1 + (peak+1);	// Handle up to 18 values above peak coded as 1 through 18
				else { memset (outbuf, 0, sizeof (double)); continue; }
				get_bits (code, 5);
			}

			// Get the sign bit and high nibble of mantissa
			sign = code >> 4;
			nibble = code & 0xF;

			// Copy 6 mantissa bytes
			memcpy (outbuf, inptr, 6), inptr += 6;

			// Output the top two bytes of the double
			uint16_t top2 = (uint16_t) ((sign << 15) + (expo << 4) + nibble);
			memcpy (outbuf+6, &top2, 2);
		}
#endif

#undef get_bits
#undef get_m48
	}
}

#endif

//--------------------------------------------------------------------
//		Read/write cache lines from/to a gwnum
//    Requires intimate knowledge of layout of gwnums in memory
//--------------------------------------------------------------------

// Structure used to pass data to read_line_slice, read_preprocess_line_slice, and write_line_slice
typedef struct {
	gwnum	*data;				// Argument passed to read_line, read_preprocess_line, and write_line
	uint64_t size;				// Argument passed to read_line, read_preprocess_line, and write_line
	CVDT	*vec;				// Argument passed to read_line, read_preprocess_line, and write_line
	int	options;			// Argument passed to read_line, read_preprocess_line, and write_line
	bool	post_process_monics;		// Argument passed to read_line, read_preprocess_line
	gwnum	*fma_data;			// FMA poly to add during write_line
	uint64_t LSWs_skipped;			// Number of LSWs to be skipped from vec during write_line
	bool	streamed_stores;		// TRUE if write_line should use streaming stores
	gwatomic blknum;			// Block number to work on when multi-threading
	uint64_t slice_size;			// Size to read or write for each block
	struct line_specific_data {
		int	line;			// Argument passed to read_line, read_preprocess_line, and write_line
		int	index;			// Adjusted line number, could be called cache_line_number
		unsigned long offset;		// Cached cache_line_offset
#ifdef SSE2
		int	imag_dist;		// Distance from real to imaginary values
#endif
	} lsd;
} read_write_line_slice_data;

void prep_line (gwhandle *gwdata, int line, read_write_line_slice_data *rwlsd);
void read_line (pmhandle *pmdata, read_write_line_slice_data *rwlsd);
void write_line (pmhandle *pmdata, read_write_line_slice_data *rwlsd);
void write_line_slice_strided (pmhandle *pmdata, read_write_line_slice_data *rwlsd, CVDT *vec, uint64_t slice_start, uint64_t slice_end, uint64_t stride_offset, uint64_t stride);
void fft_line_pass (int helper_num, pmhandle *pmdata, void *pdarg);

#if defined (AVX512)

// When multiplying small polys (a few gwnums), the overhead in prepping read_line and write_line is measurable.  This routine aims to reduce that overhead.
void prep_line (gwhandle *gwdata, int line, read_write_line_slice_data *rwlsd)
{
	// General MMGW mod requires special handling
	if (gwdata->GENERAL_MMGW_MOD) {
		int cyclic_lines = gwdata->FFTLEN * sizeof (double) / sizeof (CVDT) + 1;
		if (line < cyclic_lines) prep_line (gwdata->cyclic_gwdata, line, rwlsd);
		else {
			prep_line (gwdata->negacyclic_gwdata, line - cyclic_lines, rwlsd);
			rwlsd->lsd.line += cyclic_lines;
			rwlsd->lsd.index += cyclic_lines;
			rwlsd->lsd.offset += round_up_to_multiple_of (gwnum_datasize(gwdata->cyclic_gwdata) + GW_HEADER_SIZE(gwdata->negacyclic_gwdata), 128) / sizeof (VDT);
		}
		return;
	}

	// Pre-massage the line number and pre-calculate the cache line offset

	// Transformed real data ends up with 2 real values and FFTLEN/2-1 complex values.  Transformed complex data has FFTLEN/2 complex values.
	// Adjust line number to handle the extra data point that must be returned for transformed real data.
	int index = line;
	if (!gwdata->NEGACYCLIC_FFT) index--;

	// Check if we've read all the gwnum data
	ASSERTG (index * 16 < (int) gwdata->FFTLEN);

	// Get cache line offset
	unsigned long offset;
	if (index >= 1 || (index == 0 && gwdata->NEGACYCLIC_FFT)) offset = cache_line_offset (gwdata, index * 2) / sizeof (VDT);

	// Save for later
	rwlsd->lsd.line = line;
	rwlsd->lsd.index = index;
	rwlsd->lsd.offset = offset;
}

void read_line_slice_avx512 (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = ((read_write_line_slice_data *) rwlsd)->data;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	int	index = ((read_write_line_slice_data *) rwlsd)->lsd.index;
	int	options = ((read_write_line_slice_data *) rwlsd)->options;
	bool	post_process_monics = ((read_write_line_slice_data *) rwlsd)->post_process_monics;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;

	// Read one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if reading the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Reload vector pointer
		CVDT *vec = ((read_write_line_slice_data *) rwlsd)->vec;

		// If not post-processing monics and the input is a monic RLP, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[0].real = broadcastsd (1.0);
					vec[0].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = vec;
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
			vec++;
		}

		// If input is an RLP then read the data into the high half of vec.  We will duplicate the data as the last step.
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) vec += (size-1);

		// AVX512 implementation
		if (index == -1) {			// First real-only value and optionally seven zero-pad values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Return the first real-only value
				if (!gwdata->ZERO_PADDED_FFT) {
					vec[j].real = _mm512_set_pd (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, data[j][0]);
					vec[j].imag = broadcastsd (0.0);
				}
				// Zero padded FFTs also return the 7 FFTed values for the 7 real values stored in the header
				else {
					vec[j].real = _mm512_set_pd (data[j][-18], data[j][-17], data[j][-16], data[j][-15],
								     data[j][-14], data[j][-13], data[j][-12], data[j][0]);
					vec[j].imag = _mm512_set_pd (data[j][-24], data[j][-23], data[j][-22], data[j][-21],
								     data[j][-20], data[j][-19], 0.0, 0.0);
				}
			}
		}
		else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {	// Second real-only value and 7 complex values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				vec[j].real = _mm512_set_pd (data[j][7], data[j][6], data[j][5], data[j][4], data[j][3], data[j][2], data[j][1], data[j][8]);
				vec[j].imag = _mm512_set_pd (data[j][15], data[j][14], data[j][13], data[j][12], data[j][11], data[j][10], data[j][9], 0.0);
			}
		}
		else {				// Process complex values where imaginary part is in the high half of the cache line
			unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof(CVDT)); continue; }
				ASSERTG (FFT_state (data[j]) == FULLY_FFTed);
				vec[j].real = ((VDT *) data[j])[offset];
				vec[j].imag = ((VDT *) data[j])[offset+1];
			}
		}

		// Negate coefficients if requested
		if (options & (POLYMULT_INVEC1_NEGATE | POLYMULT_INVEC2_NEGATE)) {
			VDT zero = broadcastsd (0.0);
			for (uint64_t j = slice_start; j < slice_end; j++) {
				vec[j].real = subpd (zero, vec[j].real);
				vec[j].imag = subpd (zero, vec[j].imag);
			}
		}

		// Duplicate RLP data in reverse order
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) {
			for (uint64_t j = slice_start; j < slice_end; j++) vec[-(int64_t)j] = vec[j];
		}

		// If not post-processing monics and the input is monic, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[size].real = broadcastsd (1.0);
					vec[size].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = &vec[size];
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
		}

		// Check for completion
		if (slice_end == size) break;
	}
}

void write_line_slice_strided_avx512 (pmhandle *pmdata, read_write_line_slice_data *rwlsd, CVDT *vec, uint64_t slice_start, uint64_t slice_end, uint64_t stride_offset, uint64_t stride)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = rwlsd->data;
	gwnum	*fma_data = rwlsd->fma_data;
	int	index = rwlsd->lsd.index;
	int	options = rwlsd->options;
	CVDT	fmaval, oneval;

	// Support sliced and strided (even though not used).  An example, writing every 64th gwnum starting with the 10th gwnum in an 8K FFT.  Say we only
	// want to write gwnums with an index between 150 and 500.  So, slice_start = 150, slice_end=500, stride_offset=10, stride=64.
	// The 10th gwnum is read from vec[0], 74th from vec[1], 138th from vec[2], 202nd from vec[3].
	// Thus, j_in (index into vec) is divide_rounding_up (slice_start - stride_offset, stride) = ceil((150-10)/64) = 3.
	// And, j_out (index into data (the gwnum array)) is j_in * stride + stride_offset = 3 * 64 + 10 = 202.
	// LSWs_skipped complicates matters slightly.
	uint64_t j_in = divide_rounding_up (slice_start + rwlsd->LSWs_skipped - stride_offset, stride);
	uint64_t j_out = j_in * stride + stride_offset - rwlsd->LSWs_skipped;

	// AVX512 implementation
	union {
		VDT	a;
		double	b[8];
	} x;
	if (index == -1) {			// First real-only value and optionally seven zero-pad values
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm512_set_pd (fma_data[j_out][-18], fma_data[j_out][-17], fma_data[j_out][-16], fma_data[j_out][-15],
								     fma_data[j_out][-14], fma_data[j_out][-13], fma_data[j_out][-12], fma_data[j_out][0]),
					fmaval.imag = _mm512_set_pd (fma_data[j_out][-24], fma_data[j_out][-23], fma_data[j_out][-22], fma_data[j_out][-21],
								     fma_data[j_out][-20], fma_data[j_out][-19], 0.0, 0.0);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm512_set_pd (gwdata->GW_FFT1[-18], gwdata->GW_FFT1[-17], gwdata->GW_FFT1[-16], gwdata->GW_FFT1[-15],
								     gwdata->GW_FFT1[-14], gwdata->GW_FFT1[-13], gwdata->GW_FFT1[-12], gwdata->GW_FFT1[0]);
					oneval.imag = _mm512_set_pd (gwdata->GW_FFT1[-24], gwdata->GW_FFT1[-23], gwdata->GW_FFT1[-22], gwdata->GW_FFT1[-21],
								     gwdata->GW_FFT1[-20], gwdata->GW_FFT1[-19], 0.0, 0.0);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			// Return the first real-only value
			x.a = vec[j_in].real;
			data[j_out][0] = x.b[0];
			// Zero padded FFTs also return the 7 real values stored in the header
			if (gwdata->ZERO_PADDED_FFT) {
				data[j_out][-18] = x.b[7];
				data[j_out][-17] = x.b[6];
				data[j_out][-16] = x.b[5];
				data[j_out][-15] = x.b[4];
				data[j_out][-14] = x.b[3];
				data[j_out][-13] = x.b[2];
				data[j_out][-12] = x.b[1];
				x.a = vec[j_in].imag;
				data[j_out][-24] = x.b[7];
				data[j_out][-23] = x.b[6];
				data[j_out][-22] = x.b[5];
				data[j_out][-21] = x.b[4];
				data[j_out][-20] = x.b[3];
				data[j_out][-19] = x.b[2];
			}
		}
	}
	else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {	// Second real-only value and 7 complex values
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm512_set_pd (fma_data[j_out][7], fma_data[j_out][6], fma_data[j_out][5], fma_data[j_out][4],
								     fma_data[j_out][3], fma_data[j_out][2], fma_data[j_out][1], fma_data[j_out][8]),
					fmaval.imag = _mm512_set_pd (fma_data[j_out][15], fma_data[j_out][14], fma_data[j_out][13], fma_data[j_out][12],
								     fma_data[j_out][11], fma_data[j_out][10], fma_data[j_out][9], 0.0);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm512_set_pd (gwdata->GW_FFT1[7], gwdata->GW_FFT1[6], gwdata->GW_FFT1[5], gwdata->GW_FFT1[4],
								     gwdata->GW_FFT1[3], gwdata->GW_FFT1[2], gwdata->GW_FFT1[1], gwdata->GW_FFT1[8]);
					oneval.imag = _mm512_set_pd (gwdata->GW_FFT1[15], gwdata->GW_FFT1[14], gwdata->GW_FFT1[13], gwdata->GW_FFT1[12],
								     gwdata->GW_FFT1[11], gwdata->GW_FFT1[10], gwdata->GW_FFT1[9], 0.0);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			x.a = vec[j_in].real, data[j_out][7] = x.b[7], data[j_out][6] = x.b[6], data[j_out][5] = x.b[5], data[j_out][4] = x.b[4],
					      data[j_out][3] = x.b[3], data[j_out][2] = x.b[2], data[j_out][1] = x.b[1], data[j_out][8] = x.b[0];
			x.a = vec[j_in].imag, data[j_out][15] = x.b[7], data[j_out][14] = x.b[6], data[j_out][13] = x.b[5], data[j_out][12] = x.b[4],
					      data[j_out][11] = x.b[3], data[j_out][10] = x.b[2], data[j_out][9] = x.b[1];
		}
	}
	else {				// Process complex values where imaginary part is in the high half of the cache line
		unsigned long offset = rwlsd->lsd.offset;
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else fmaval.real = ((VDT *) fma_data[j_out])[offset], fmaval.imag = ((VDT *) fma_data[j_out])[offset+1];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = ((VDT *) gwdata->GW_FFT1)[offset], oneval.imag = ((VDT *) gwdata->GW_FFT1)[offset+1];
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			if (rwlsd->streamed_stores) {
				streampd (&((VDT *) data[j_out])[offset], vec[j_in].real);
				streampd (&((VDT *) data[j_out])[offset+1], vec[j_in].imag);
			} else {
				((VDT *) data[j_out])[offset] = vec[j_in].real;
				((VDT *) data[j_out])[offset+1] = vec[j_in].imag;
			}
		}
	}
}

void write_line_slice_avx512 (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;
	CVDT	*vec = ((read_write_line_slice_data *) rwlsd)->vec;

	// Write one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if writing the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Perform an unstrided write
		write_line_slice_strided (pmdata, (read_write_line_slice_data *) rwlsd, vec, slice_start, slice_end, 0, 1);

		// Check for completion
		if (slice_end == size) break;
	}
}

#elif defined (FMA)

#elif defined (AVX)

// When multiplying small polys (a few gwnums), the overhead in prepping read_line and write_line is measurable.  This routine aims to reduce that overhead.
void prep_line (gwhandle *gwdata, int line, read_write_line_slice_data *rwlsd)
{
	// General MMGW mod requires special handling
	if (gwdata->GENERAL_MMGW_MOD) {
		int cyclic_lines = gwdata->FFTLEN * sizeof (double) / sizeof (CVDT) + 1;
		if (line < cyclic_lines) prep_line (gwdata->cyclic_gwdata, line, rwlsd);
		else {
			prep_line (gwdata->negacyclic_gwdata, line - cyclic_lines, rwlsd);
			rwlsd->lsd.line += cyclic_lines;
			rwlsd->lsd.index += cyclic_lines;
			rwlsd->lsd.offset += round_up_to_multiple_of (gwnum_datasize(gwdata->cyclic_gwdata) + GW_HEADER_SIZE(gwdata->negacyclic_gwdata), 128) / sizeof (VDT);
		}
		return;
	}

	// Pre-massage the line number and pre-calculate the cache line offset

	// Transformed real data ends up with 2 real values and FFTLEN/2-1 complex values.  Transformed complex data has FFTLEN/2 complex values.
	// Adjust line number to handle the extra data point that must be returned for transformed real data.
	int index = line;
	if (!gwdata->NEGACYCLIC_FFT) index--;

	// Zero padded FFTs return another cache line for processing the 7 real values stored in the header
	if (gwdata->ZERO_PADDED_FFT) index--;

	// Check if we've read all the gwnum data
	ASSERTG (index * 8 < (int) gwdata->FFTLEN);

	// Get cache line offset
	unsigned long offset;
	if (index >= 1 || (index == 0 && gwdata->NEGACYCLIC_FFT)) offset = cache_line_offset (gwdata, index) / sizeof (VDT);

	// Save for later
	rwlsd->lsd.line = line;
	rwlsd->lsd.index = index;
	rwlsd->lsd.offset = offset;
}

void read_line_slice_avx (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = ((read_write_line_slice_data *) rwlsd)->data;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	int	index = ((read_write_line_slice_data *) rwlsd)->lsd.index;
	int	options = ((read_write_line_slice_data *) rwlsd)->options;
	bool	post_process_monics = ((read_write_line_slice_data *) rwlsd)->post_process_monics;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;

	// Read one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if reading the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Reload vector pointer
		CVDT *vec = ((read_write_line_slice_data *) rwlsd)->vec;

		// If not post-processing monics and the input is a monic RLP, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[0].real = broadcastsd (1.0);
					vec[0].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = vec;
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
			vec++;
		}

		// If input is an RLP then read the data into the high half of vec.  We will duplicate the data as the last step.
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) vec += (size-1);

		// AVX implementation
		if (index == -2) {			// Four of the seven FFTed zero pad header values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Zero padded FFTs, return 4 of the FFTed values for the 7 real values stored in the header
				vec[j].real = _mm256_set_pd (data[j][-18], data[j][-17], data[j][-16], data[j][-15]);
				vec[j].imag = _mm256_set_pd (data[j][-24], data[j][-23], data[j][-22], data[j][-21]);
			}
		}
		else if (index == -1) {			// First real-only value and optionally three of the seven FFTed zero-pad values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Return the first real-only value
				if (!gwdata->ZERO_PADDED_FFT) {
					vec[j].real = _mm256_set_pd (0.0, 0.0, 0.0, data[j][0]);
					vec[j].imag = broadcastsd (0.0);
				}
				// Zero padded FFTs also return 3 of the FFTed values for the 7 real values stored in the header
				else {
					vec[j].real = _mm256_set_pd (data[j][-14], data[j][-13], data[j][-12], data[j][0]);
					vec[j].imag = _mm256_set_pd (data[j][-20], data[j][-19], 0.0, 0.0);
				}
			}
		}
		else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {	// Second real-only value and 3 complex values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				vec[j].real = _mm256_set_pd (data[j][3], data[j][2], data[j][1], data[j][4]);
				vec[j].imag = _mm256_set_pd (data[j][7], data[j][6], data[j][5], 0.0);
			}
		}
		else {				// Process complex values where imaginary part is in the high half of the cache line
			unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				ASSERTG (FFT_state (data[j]) == FULLY_FFTed);
				vec[j].real = ((VDT *) data[j])[offset];
				vec[j].imag = ((VDT *) data[j])[offset+1];
			}
		}

		// Negate coefficients if requested
		if (options & (POLYMULT_INVEC1_NEGATE | POLYMULT_INVEC2_NEGATE)) {
			VDT zero = broadcastsd (0.0);
			for (uint64_t j = slice_start; j < slice_end; j++) {
				vec[j].real = subpd (zero, vec[j].real);
				vec[j].imag = subpd (zero, vec[j].imag);
			}
		}

		// Duplicate RLP data in reverse order
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) {
			for (uint64_t j = slice_start; j < slice_end; j++) vec[-(int64_t)j] = vec[j];
		}

		// If not post-processing monics and the input is monic, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[size].real = broadcastsd (1.0);
					vec[size].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = &vec[size];
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
		}

		// Check for completion
		if (slice_end == size) break;
	}
}

void write_line_slice_strided_avx (pmhandle *pmdata, read_write_line_slice_data *rwlsd, CVDT *vec, uint64_t slice_start, uint64_t slice_end, uint64_t stride_offset, uint64_t stride)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = rwlsd->data;
	gwnum	*fma_data = rwlsd->fma_data;
	uint64_t index = rwlsd->lsd.index;
	int	options = rwlsd->options;
	CVDT	fmaval, oneval;

	// Support sliced and strided (even though not used).  An example, writing every 64th gwnum starting with the 10th gwnum in an 8K FFT.  Say we only
	// want to write gwnums with an index between 150 and 500.  So, slice_start = 150, slice_end=500, stride_offset=10, stride=64.
	// The 10th gwnum is read from vec[0], 74th from vec[1], 138th from vec[2], 202nd from vec[3].
	// Thus, j_in (index into vec) is divide_rounding_up (slice_start - stride_offset, stride) = ceil((150-10)/64) = 3.
	// And, j_out (index into data (the gwnum array)) is j_in * stride + stride_offset = 3 * 64 + 10 = 202.
	// LSWs_skipped complicates matters slightly.
	uint64_t j_in = divide_rounding_up (slice_start + rwlsd->LSWs_skipped - stride_offset, stride);
	uint64_t j_out = j_in * stride + stride_offset - rwlsd->LSWs_skipped;

	// AVX implementation
	union {
		VDT	a;
		double	b[4];
	} x;
	if (index == -2) {			// Four of the seven zero pad value in the header
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm256_set_pd (fma_data[j_out][-18], fma_data[j_out][-17], fma_data[j_out][-16], fma_data[j_out][-15]),
					fmaval.imag = _mm256_set_pd (fma_data[j_out][-24], fma_data[j_out][-23], fma_data[j_out][-22], fma_data[j_out][-21]);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm256_set_pd (gwdata->GW_FFT1[-18], gwdata->GW_FFT1[-17], gwdata->GW_FFT1[-16], gwdata->GW_FFT1[-15]),
					oneval.imag = _mm256_set_pd (gwdata->GW_FFT1[-24], gwdata->GW_FFT1[-23], gwdata->GW_FFT1[-22], gwdata->GW_FFT1[-21]);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			x.a = vec[j_in].real;
			data[j_out][-18] = x.b[3];
			data[j_out][-17] = x.b[2];
			data[j_out][-16] = x.b[1];
			data[j_out][-15] = x.b[0];
			x.a = vec[j_in].imag;
			data[j_out][-24] = x.b[3];
			data[j_out][-23] = x.b[2];
			data[j_out][-22] = x.b[1];
			data[j_out][-21] = x.b[0];
		}
	}
	else if (index == -1) {			// First real-only value and optionally three of the seven zero-pad values
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm256_set_pd (fma_data[j_out][-14], fma_data[j_out][-13], fma_data[j_out][-12], fma_data[j_out][0]),
					fmaval.imag = _mm256_set_pd (fma_data[j_out][-20], fma_data[j_out][-19], 0.0, 0.0);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm256_set_pd (gwdata->GW_FFT1[-14], gwdata->GW_FFT1[-13], gwdata->GW_FFT1[-12], gwdata->GW_FFT1[0]),
					oneval.imag = _mm256_set_pd (gwdata->GW_FFT1[-20], gwdata->GW_FFT1[-19], 0.0, 0.0);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			// Return the first real-only value
			x.a = vec[j_in].real;
			data[j_out][0] = x.b[0];
			// Return 3 of the 7 real values stored in the header
			if (gwdata->ZERO_PADDED_FFT) {
				data[j_out][-14] = x.b[3];
				data[j_out][-13] = x.b[2];
				data[j_out][-12] = x.b[1];
				x.a = vec[j_in].imag;
				data[j_out][-20] = x.b[3];
				data[j_out][-19] = x.b[2];
			}
		}
	}
	else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {			// Second real-only value
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm256_set_pd (fma_data[j_out][3], fma_data[j_out][2], fma_data[j_out][1], fma_data[j_out][4]),
					fmaval.imag = _mm256_set_pd (fma_data[j_out][7], fma_data[j_out][6], fma_data[j_out][5], 0.0);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm256_set_pd (gwdata->GW_FFT1[3], gwdata->GW_FFT1[2], gwdata->GW_FFT1[1], gwdata->GW_FFT1[4]),
					oneval.imag = _mm256_set_pd (gwdata->GW_FFT1[7], gwdata->GW_FFT1[6], gwdata->GW_FFT1[5], 0.0);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			x.a = vec[j_in].real, data[j_out][3] = x.b[3], data[j_out][2] = x.b[2], data[j_out][1] = x.b[1], data[j_out][4] = x.b[0];
			x.a = vec[j_in].imag, data[j_out][7] = x.b[3], data[j_out][6] = x.b[2], data[j_out][5] = x.b[1];
		}
	}
	else {				// Process complex values where imaginary part is in the high half of the cache line
		unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else fmaval.real = ((VDT *) fma_data[j_out])[offset], fmaval.imag = ((VDT *) fma_data[j_out])[offset+1];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = ((VDT *) gwdata->GW_FFT1)[offset], oneval.imag = ((VDT *) gwdata->GW_FFT1)[offset+1];
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			if (rwlsd->streamed_stores) {
				streampd (&((VDT *) data[j_out])[offset], vec[j_in].real);
				streampd (&((VDT *) data[j_out])[offset+1], vec[j_in].imag);
			} else {
				((VDT *) data[j_out])[offset] = vec[j_in].real;
				((VDT *) data[j_out])[offset+1] = vec[j_in].imag;
			}
		}
	}
}

void write_line_slice_avx (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;
	CVDT	*vec = ((read_write_line_slice_data *) rwlsd)->vec;

	// Write one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if writing the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Perform an unstrided write
		write_line_slice_strided (pmdata, (read_write_line_slice_data *) rwlsd, vec, slice_start, slice_end, 0, 1);

		// Check for completion
		if (slice_end == size) break;
	}
}

#elif defined (SSE2)

// When multiplying small polys (a few gwnums), the overhead in prepping read_line and write_line is measurable.  This routine aims to reduce that overhead.
void prep_line (gwhandle *gwdata, int line, read_write_line_slice_data *rwlsd)
{
	// General MMGW mod requires special handling
	if (gwdata->GENERAL_MMGW_MOD) {
		int cyclic_lines = gwdata->FFTLEN * sizeof (double) / sizeof (CVDT) + 1;
		if (line < cyclic_lines) prep_line (gwdata->cyclic_gwdata, line, rwlsd);
		else {
			prep_line (gwdata->negacyclic_gwdata, line - cyclic_lines, rwlsd);
			rwlsd->lsd.line += cyclic_lines;
			rwlsd->lsd.index += cyclic_lines;
			rwlsd->lsd.offset += round_up_to_multiple_of (gwnum_datasize(gwdata->cyclic_gwdata) + GW_HEADER_SIZE(gwdata->negacyclic_gwdata), 128) / sizeof (VDT);
		}
		return;
	}

	// Pre-massage the index and pre-calculate the cache line offset

	// Transformed real data ends up with 2 real values and FFTLEN/2-1 complex values.  Transformed complex data has FFTLEN/2 complex values.
	// Adjust index to handle the extra data point that must be returned for transformed real data.
	int index = line;
	if (!gwdata->NEGACYCLIC_FFT) index--;

	// Zero padded FFTs return another 3 cache lines for processing the 7 real values stored in the header
	if (gwdata->ZERO_PADDED_FFT) index -= 3;

	// Check if we've read all the gwnum data
	ASSERTG (index * 4 < (int) gwdata->FFTLEN);

	// Various SSE2 FFT implementations store the imaginary part of a complex number either 8, 16, or 32 bytes above the real part
	int	imag_dist;
	if (gwdata->FFT_TYPE > 0 &&					// r4, r4delay, r4dwpn with 8-complex final levels
	    (gwdata->PASS2_SIZE == 1536 || gwdata->PASS2_SIZE == 2048 || gwdata->PASS2_SIZE == 2560 ||
	     gwdata->PASS2_SIZE == 4608 || gwdata->PASS2_SIZE == 6144 || gwdata->PASS2_SIZE == 7680 ||
	     gwdata->PASS2_SIZE == 8192 || gwdata->PASS2_SIZE == 10240 || gwdata->PASS2_SIZE == 12800)) imag_dist = 32;
	else if (gwdata->NEGACYCLIC_FFT) imag_dist = 16;
	else if (gwdata->PASS2_SIZE == 0 && index < 4) imag_dist = 8;
	else if (gwdata->PASS2_SIZE != 0 && gwdata->FFT_TYPE == 0 && (index <= 1 || index == 4 || index == 5)) imag_dist = 8;	// hg ffts
	else imag_dist = 16;

	// Get cache line offset
	unsigned long offset;
	if (index >= 1 || (index == 0 && gwdata->NEGACYCLIC_FFT)) offset = cache_line_offset (gwdata, index / 2) / sizeof (VDT);

	// Save for later
	rwlsd->lsd.line = line;
	rwlsd->lsd.index = index;
	rwlsd->lsd.offset = offset;
	rwlsd->lsd.imag_dist = imag_dist;
}

void read_line_slice_sse2 (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = ((read_write_line_slice_data *) rwlsd)->data;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	int	index = ((read_write_line_slice_data *) rwlsd)->lsd.index;
	int	options = ((read_write_line_slice_data *) rwlsd)->options;
	bool	post_process_monics = ((read_write_line_slice_data *) rwlsd)->post_process_monics;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;
	int	imag_dist = ((read_write_line_slice_data *) rwlsd)->lsd.imag_dist;

	// Read one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if reading the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Reload vector pointer
		CVDT *vec = ((read_write_line_slice_data *) rwlsd)->vec;

		// If not post-processing monics and the input is a monic RLP, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[0].real = broadcastsd (1.0);
					vec[0].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = vec;
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
			vec++;
		}

		// If input is an RLP then read the data into the high half of vec.  We will duplicate the data as the last step.
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) vec += (size-1);

		// SSE2 implementation
		if (index <= -2) {			// Six of the seven zero pad header values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Zero padded FFTs, return 1 of the FFTed values for the 7 real values stored in the header
				vec[j].real = _mm_set_pd (data[j][index*2-10], data[j][index*2-9]);
				vec[j].imag = _mm_set_pd (data[j][index*2-16], data[j][index*2-15]);
			}
		}
		else if (index == -1) {			// First real-only valueand optionally one of the seven zero-pad values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Return the first real-only value
				if (!gwdata->ZERO_PADDED_FFT) {
					vec[j].real = _mm_set_pd (0.0, data[j][0]);
					vec[j].imag = broadcastsd (0.0);
				}
				// Zero padded FFTs also return 1 of the FFTed values for the 7 real values stored in the header
				else {
					vec[j].real = _mm_set_pd (data[j][-12], data[j][0]);
					vec[j].imag = broadcastsd (0.0);
				}
			}
		}
		else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {	// Second real-only value and one complex value
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				if (imag_dist == 8) {
					vec[j].real = _mm_set_pd (data[j][2], data[j][1]);
					vec[j].imag = _mm_set_pd (data[j][3], 0.0);
				} else if (imag_dist == 16) {
					vec[j].real = _mm_set_pd (data[j][1], data[j][2]);
					vec[j].imag = _mm_set_pd (data[j][3], 0.0);
				} else {
					vec[j].real = _mm_set_pd (data[j][1], data[j][4]);
					vec[j].imag = _mm_set_pd (data[j][5], 0.0);
				}
			}
		}
		else {				// Process complex values where imaginary part is 8, 16, or 32 bytes above the real part
			unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				ASSERTG (FFT_state (data[j]) == FULLY_FFTed);
				if (imag_dist == 8) {
					vec[j].real = _mm_set_pd (data[j][index*4+2], data[j][index*4]);
					vec[j].imag = _mm_set_pd (data[j][index*4+3], data[j][index*4+1]);
				} else if (imag_dist == 16) {
					vec[j].real = ((VDT *) data[j])[offset+(index&1)*2];
					vec[j].imag = ((VDT *) data[j])[offset+(index&1)*2+1];
				} else {
					vec[j].real = ((VDT *) data[j])[offset+(index&1)];
					vec[j].imag = ((VDT *) data[j])[offset+(index&1)+2];
				}
			}
		}

		// Negate coefficients if requested
		if (options & (POLYMULT_INVEC1_NEGATE | POLYMULT_INVEC2_NEGATE)) {
			VDT zero = broadcastsd (0.0);
			for (uint64_t j = slice_start; j < slice_end; j++) {
				vec[j].real = subpd (zero, vec[j].real);
				vec[j].imag = subpd (zero, vec[j].imag);
			}
		}

		// Duplicate RLP data in reverse order
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) {
			for (uint64_t j = slice_start; j < slice_end; j++) vec[-(int64_t)j] = vec[j];
		}

		// If not post-processing monics and the input is monic, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[size].real = broadcastsd (1.0);
					vec[size].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = &vec[size];
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
		}

		// Check for completion
		if (slice_end == size) break;
	}
}

void write_line_slice_strided_sse2 (pmhandle *pmdata, read_write_line_slice_data *rwlsd, CVDT *vec, uint64_t slice_start, uint64_t slice_end, uint64_t stride_offset, uint64_t stride)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = rwlsd->data;
	gwnum	*fma_data = rwlsd->fma_data;
	int	index = rwlsd->lsd.index;
	int	options = rwlsd->options;
	int	imag_dist = rwlsd->lsd.imag_dist;
	CVDT	fmaval, oneval;

	// Support sliced and strided (even though not used).  An example, writing every 64th gwnum starting with the 10th gwnum in an 8K FFT.  Say we only
	// want to write gwnums with an index between 150 and 500.  So, slice_start = 150, slice_end=500, stride_offset=10, stride=64.
	// The 10th gwnum is read from vec[0], 74th from vec[1], 138th from vec[2], 202nd from vec[3].
	// Thus, j_in (index into vec) is divide_rounding_up (slice_start - stride_offset, stride) = ceil((150-10)/64) = 3.
	// And, j_out (index into data (the gwnum array)) is j_in * stride + stride_offset = 3 * 64 + 10 = 202.
	// LSWs_skipped complicates matters slightly.
	uint64_t j_in = divide_rounding_up (slice_start + rwlsd->LSWs_skipped - stride_offset, stride);
	uint64_t j_out = j_in * stride + stride_offset - rwlsd->LSWs_skipped;

	// SSE2 instructions implementation
	union {
		VDT	a;
		double	b[2];
	} x;
	if (index <= -2) {		// Six of the seven real values in the header
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else
					fmaval.real = _mm_set_pd (fma_data[j_out][index*2-10], fma_data[j_out][index*2-9]),
					fmaval.imag = _mm_set_pd (fma_data[j_out][index*2-16], fma_data[j_out][index*2-15]);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm_set_pd (gwdata->GW_FFT1[index*2-10], gwdata->GW_FFT1[index*2-9]),
					oneval.imag = _mm_set_pd (gwdata->GW_FFT1[index*2-16], gwdata->GW_FFT1[index*2-15]);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			x.a = vec[j_in].real;
			data[j_out][index*2-10] = x.b[1];
			data[j_out][index*2-9] = x.b[0];
			x.a = vec[j_in].imag;
			data[j_out][index*2-16] = x.b[1];
			data[j_out][index*2-15] = x.b[0];
		}
	}
	else if (index == -1) {		// First real-only value and optionally one of the seven zero-pad values
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else fmaval.real = _mm_set_pd (fma_data[j_out][-12], fma_data[j_out][0]);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = _mm_set_pd (gwdata->GW_FFT1[-12], gwdata->GW_FFT1[0]);
					fmaval.real = mulpd (fmaval.real, oneval.real);
				}
				if (options & POLYMULT_FMADD) vec[j_in].real = addpd (vec[j_in].real, fmaval.real);
				else if (options & POLYMULT_FMSUB) vec[j_in].real = subpd (vec[j_in].real, fmaval.real);
				else vec[j_in].real = subpd (fmaval.real, vec[j_in].real);
			}
			// Return the first real-only value
			x.a = vec[j_in].real;
			data[j_out][0] = x.b[0];
			// Return 1 of the 7 real values stored in the header
			if (gwdata->ZERO_PADDED_FFT) data[j_out][-12] = x.b[1];
		}
	}
	else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {		// Second real-only value
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else if (imag_dist == 8)
					fmaval.real = _mm_set_pd (fma_data[j_out][2], fma_data[j_out][1]), fmaval.imag = _mm_set_pd (fma_data[j_out][3], 0.0);
				else if (imag_dist == 16)
					fmaval.real = _mm_set_pd (fma_data[j_out][1], fma_data[j_out][2]), fmaval.imag = _mm_set_pd (fma_data[j_out][3], 0.0);
				else
					fmaval.real = _mm_set_pd (fma_data[j_out][1], fma_data[j_out][4]), fmaval.imag = _mm_set_pd (fma_data[j_out][5], 0.0);
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					if (imag_dist == 8)
						oneval.real = _mm_set_pd (gwdata->GW_FFT1[2], gwdata->GW_FFT1[1]),
						oneval.imag = _mm_set_pd (gwdata->GW_FFT1[3], 0.0);
					else if (imag_dist == 16)
						oneval.real = _mm_set_pd (gwdata->GW_FFT1[1], gwdata->GW_FFT1[2]),
						oneval.imag = _mm_set_pd (gwdata->GW_FFT1[3], 0.0);
					else
						oneval.real = _mm_set_pd (gwdata->GW_FFT1[1], gwdata->GW_FFT1[4]),
						oneval.imag = _mm_set_pd (gwdata->GW_FFT1[5], 0.0);
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			if (imag_dist == 8) {
				x.a = vec[j_in].real, data[j_out][2] = x.b[1], data[j_out][1] = x.b[0];
				x.a = vec[j_in].imag, data[j_out][3] = x.b[1];
			} else if (imag_dist == 16) {
				x.a = vec[j_in].real, data[j_out][1] = x.b[1], data[j_out][2] = x.b[0];
				x.a = vec[j_in].imag, data[j_out][3] = x.b[1];
			} else {
				x.a = vec[j_in].real, data[j_out][1] = x.b[1], data[j_out][4] = x.b[0];
				x.a = vec[j_in].imag, data[j_out][5] = x.b[1];
			}
		}
	}
	else {				// Process complex values where imaginary part is 8 or 16 bytes above the real part
		unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL)
					fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else if (imag_dist == 8)
					fmaval.real = _mm_set_pd (fma_data[j_out][index*4+2], fma_data[j_out][index*4]),
					fmaval.imag = _mm_set_pd (fma_data[j_out][index*4+3], fma_data[j_out][index*4+1]);
				else if (imag_dist == 16)
					fmaval.real = ((VDT *) fma_data[j_out])[offset+(index&1)*2],
					fmaval.imag = ((VDT *) fma_data[j_out])[offset+(index&1)*2+1];
				else 
					fmaval.real = ((VDT *) fma_data[j_out])[offset+(index&1)],
					fmaval.imag = ((VDT *) fma_data[j_out])[offset+(index&1)+2];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					if (imag_dist == 8)
						oneval.real = _mm_set_pd (gwdata->GW_FFT1[index*4+2], gwdata->GW_FFT1[index*4]),
						oneval.imag = _mm_set_pd (gwdata->GW_FFT1[index*4+3], gwdata->GW_FFT1[index*4+1]);
					else if (imag_dist == 16)
						oneval.real = ((VDT *) gwdata->GW_FFT1)[offset+(index&1)*2],
						oneval.imag = ((VDT *) gwdata->GW_FFT1)[offset+(index&1)*2+1];
					else 
						oneval.real = ((VDT *) gwdata->GW_FFT1)[offset+(index&1)],
						oneval.imag = ((VDT *) gwdata->GW_FFT1)[offset+(index&1)+2];
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			if (imag_dist == 8) {
				x.a = vec[j_in].real, data[j_out][index*4+2] = x.b[1], data[j_out][index*4] = x.b[0];
				x.a = vec[j_in].imag, data[j_out][index*4+3] = x.b[1], data[j_out][index*4+1] = x.b[0];
			} else if (imag_dist == 16) {
				((VDT *) data[j_out])[offset+(index&1)*2] = vec[j_in].real;
				((VDT *) data[j_out])[offset+(index&1)*2+1] = vec[j_in].imag;
			} else {
				((VDT *) data[j_out])[offset+(index&1)] = vec[j_in].real;
				((VDT *) data[j_out])[offset+(index&1)+2] = vec[j_in].imag;
			}
		}
	}
}

void write_line_slice_sse2 (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;
	CVDT	*vec = ((read_write_line_slice_data *) rwlsd)->vec;

	// Write one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if writing the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Perform an unstrided write
		write_line_slice_strided (pmdata, (read_write_line_slice_data *) rwlsd, vec, slice_start, slice_end, 0, 1);

		// Check for completion
		if (slice_end == size) break;
	}
}

#elif !defined (X86_64)				// x87 FFTs only supported in 32-bit mode

// When multiplying small polys (a few gwnums), the overhead in prepping read_line and write_line is measurable.  This routine aims to reduce that overhead.
void prep_line (gwhandle *gwdata, int line, read_write_line_slice_data *rwlsd)
{
	// General MMGW mod requires special handling
	if (gwdata->GENERAL_MMGW_MOD) {
		int cyclic_lines = gwdata->FFTLEN * sizeof (double) / sizeof (CVDT) + 1;
		if (line < cyclic_lines) prep_line (gwdata->cyclic_gwdata, line, rwlsd);
		else {
			prep_line (gwdata->negacyclic_gwdata, line - cyclic_lines, rwlsd);
			rwlsd->lsd.line += cyclic_lines;
			rwlsd->lsd.index += cyclic_lines;
			rwlsd->lsd.offset += round_up_to_multiple_of (gwnum_datasize(gwdata->cyclic_gwdata) + GW_HEADER_SIZE(gwdata->negacyclic_gwdata), 128) / sizeof (VDT);
		}
		return;
	}

	// Pre-massage the line number and pre-calculate the cache line offset

	// Transformed real data ends up with 2 real values and FFTLEN/2-1 complex values.  Transformed complex data has FFTLEN/2 complex values.
	// Adjust line number to handle the extra data point that must be returned for transformed real data.
	int index = line;
	if (!gwdata->NEGACYCLIC_FFT) index--;

	// Zero padded FFTs must also return for processing the 7 real values stored in the header
	if (gwdata->ZERO_PADDED_FFT) index -= 7;

	// Check if we've read all the gwnum data
	ASSERTG (index * 2 < (int) gwdata->FFTLEN);

	// Get cache line offset
	unsigned long offset;
	if (index >= 1 || (index == 0 && gwdata->NEGACYCLIC_FFT)) offset = cache_line_offset (gwdata, index / 4) / sizeof (VDT);

	// Save for later
	rwlsd->lsd.line = line;
	rwlsd->lsd.index = index;
	rwlsd->lsd.offset = offset;
}

void read_line_slice_dbl (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = ((read_write_line_slice_data *) rwlsd)->data;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	int	index = ((read_write_line_slice_data *) rwlsd)->lsd.index;
	int	options = ((read_write_line_slice_data *) rwlsd)->options;
	bool	post_process_monics = ((read_write_line_slice_data *) rwlsd)->post_process_monics;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;

	// Read one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if reading the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Reload vector pointer
		CVDT *vec = ((read_write_line_slice_data *) rwlsd)->vec;

		// If not post-processing monics and the input is a monic RLP, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[0].real = broadcastsd (1.0);
					vec[0].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = vec;
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
			vec++;
		}

		// If input is an RLP then read the data into the high half of vec.  We will duplicate the data as the last step.
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) vec += (size-1);

		// No special instructions implementation
		if (index <= -2) {			// Seven zero pad header values
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				// Zero padded FFTs return 1 of the FFTed values for the 7 real values stored in the header
				vec[j].real = data[j][index-10];
				vec[j].imag = data[j][index-17];
			}
		}
		else if (index == -1) {			// First real-only value
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				vec[j].real = data[j][0];
				vec[j].imag = 0.0;
			}
		}
		else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {			// Second real-only value
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				if (gwdata->PASS2_SIZE == 0) vec[j].real = data[j][1];
				else vec[j].real = data[j][2];
				vec[j].imag = 0.0;
			}
		}
		else {				// Process complex values where imaginary part is 8 or 16 bytes above the real part
			unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
			for (uint64_t j = slice_start; j < slice_end; j++) {
				if (data[j] == NULL) { memset (&vec[j], 0, sizeof (CVDT)); continue; }
				ASSERTG (FFT_state (data[j]) == FULLY_FFTed);
				if (gwdata->PASS2_SIZE == 0 && index < 8 && !gwdata->NEGACYCLIC_FFT) {
					vec[j].real = data[j][offset+(index&3)*2];
					vec[j].imag = data[j][offset+(index&3)*2+1];
				} else {
					vec[j].real = data[j][offset+(index&2)*2+(index&1)];
					vec[j].imag = data[j][offset+(index&2)*2+(index&1)+2];
				}
			}
		}

		// Negate coefficients if requested
		if (options & (POLYMULT_INVEC1_NEGATE | POLYMULT_INVEC2_NEGATE)) {
			for (uint64_t j = slice_start; j < slice_end; j++) {
				vec[j].real = -vec[j].real;
				vec[j].imag = -vec[j].imag;
			}
		}

		// Duplicate RLP data in reverse order
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) {
			for (uint64_t j = slice_start; j < slice_end; j++) vec[-(int64_t)j] = vec[j];
		}

		// If not post-processing monics and the input is monic, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[size].real = broadcastsd (1.0);
					vec[size].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd = * (read_write_line_slice_data *) rwlsd;
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = &vec[size];
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
		}

		// Check for completion
		if (slice_end == size) break;
	}
}

void write_line_slice_strided_dbl (pmhandle *pmdata, read_write_line_slice_data *rwlsd, CVDT *vec, uint64_t slice_start, uint64_t slice_end, uint64_t stride_offset, uint64_t stride)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = rwlsd->data;
	gwnum	*fma_data = rwlsd->fma_data;
	int	index = rwlsd->lsd.index;
	int	options = rwlsd->options;
	CVDT	fmaval, oneval;

	// Support sliced and strided (even though not used).  An example, writing every 64th gwnum starting with the 10th gwnum in an 8K FFT.  Say we only
	// want to write gwnums with an index between 150 and 500.  So, slice_start = 150, slice_end=500, stride_offset=10, stride=64.
	// The 10th gwnum is read from vec[0], 74th from vec[1], 138th from vec[2], 202nd from vec[3].
	// Thus, j_in (index into vec) is divide_rounding_up (slice_start - stride_offset, stride) = ceil((150-10)/64) = 3.
	// And, j_out (index into data (the gwnum array)) is j_in * stride + stride_offset = 3 * 64 + 10 = 202.
	// LSWs_skipped complicates matters slightly.
	uint64_t j_in = divide_rounding_up (slice_start + rwlsd->LSWs_skipped - stride_offset, stride);
	uint64_t j_out = j_in * stride + stride_offset - rwlsd->LSWs_skipped;

	// No special instructions implementation
	if (index <= -2) {			// Seven zero pad header values
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else fmaval.real = fma_data[j_out][index-10], fmaval.imag = fma_data[j_out][index-17];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = gwdata->GW_FFT1[index-10], oneval.imag = gwdata->GW_FFT1[index-17];
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			data[j_out][index-10] = vec[j_in].real;
			data[j_out][index-17] = vec[j_in].imag;
		}
	}
	else if (index == -1) {			// First real-only value
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0);
				else fmaval.real = fma_data[j_out][0];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = gwdata->GW_FFT1[0];
					fmaval.real = mulpd (fmaval.real, oneval.real);
				}
				if (options & POLYMULT_FMADD) vec[j_in].real = vec[j_in].real + fmaval.real;
				else if (options & POLYMULT_FMSUB) vec[j_in].real = vec[j_in].real - fmaval.real;
				else vec[j_in].real = fmaval.real - vec[j_in].real;
			}
			data[j_out][0] = vec[j_in].real;
		}
	}
	else if (index == 0 && !gwdata->NEGACYCLIC_FFT) {		// Second real-only value
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0);
				else if (gwdata->PASS2_SIZE == 0) fmaval.real = fma_data[j_out][1];
				else fmaval.real = fma_data[j_out][2];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					oneval.real = gwdata->GW_FFT1[2];
					fmaval.real = mulpd (fmaval.real, oneval.real);
				}
				if (options & POLYMULT_FMADD) vec[j_in].real = vec[j_in].real + fmaval.real;
				else if (options & POLYMULT_FMSUB) vec[j_in].real = vec[j_in].real - fmaval.real;
				else vec[j_in].real = fmaval.real - vec[j_in].real;
			}
			if (gwdata->PASS2_SIZE == 0) data[j_out][1] = vec[j_in].real;
			else data[j_out][2] = vec[j_in].real;
		}
	}
	else {				// Process complex values where imaginary part is 8 or 16 bytes above the real part
		unsigned long offset = ((read_write_line_slice_data *) rwlsd)->lsd.offset;
		for ( ; j_out < slice_end; j_out += stride, j_in++) {
			if (data[j_out] == NULL) continue;
			if (options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) {
				if (fma_data == NULL || fma_data[j_out] == NULL) fmaval.real = broadcastsd (0.0), fmaval.imag = broadcastsd (0.0);
				else if (gwdata->PASS2_SIZE == 0 && index < 8 && !gwdata->NEGACYCLIC_FFT)
					fmaval.real = fma_data[j_out][offset+(index&3)*2], fmaval.imag = fma_data[j_out][offset+(index&3)*2+1];
				else
					fmaval.real = fma_data[j_out][offset+(index&2)*2+(index&1)], fmaval.imag = fma_data[j_out][offset+(index&2)*2+(index&1)+2];
				if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
					if (gwdata->PASS2_SIZE == 0 && index < 8 && !gwdata->NEGACYCLIC_FFT)
						oneval.real = gwdata->GW_FFT1[offset+(index&3)*2], oneval.imag = gwdata->GW_FFT1[offset+(index&3)*2+1];
					else
						oneval.real = gwdata->GW_FFT1[offset+(index&2)*2+(index&1)], oneval.imag = gwdata->GW_FFT1[offset+(index&2)*2+(index&1)+2];
					cvmul (fmaval, fmaval, oneval);
				}
				if (options & POLYMULT_FMADD) cvadd (vec[j_in], vec[j_in], fmaval);
				else if (options & POLYMULT_FMSUB) cvsub (vec[j_in], vec[j_in], fmaval);
				else cvsub (vec[j_in], fmaval, vec[j_in]);
			}
			if (gwdata->PASS2_SIZE == 0 && index < 8 && !gwdata->NEGACYCLIC_FFT) {
				data[j_out][offset+(index&3)*2] = vec[j_in].real;
				data[j_out][offset+(index&3)*2+1] = vec[j_in].imag;
			} else {
				data[j_out][offset+(index&2)*2+(index&1)] = vec[j_in].real;
				data[j_out][offset+(index&2)*2+(index&1)+2] = vec[j_in].imag;
			}
		}
	}
}

void write_line_slice_dbl (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	uint64_t slice_size = ((read_write_line_slice_data *) rwlsd)->slice_size;
	CVDT	*vec = ((read_write_line_slice_data *) rwlsd)->vec;

	// Write one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if writing the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Perform an unstrided write
		write_line_slice_strided (pmdata, (read_write_line_slice_data *) rwlsd, vec, slice_start, slice_end, 0, 1);

		// Check for completion
		if (slice_end == size) break;
	}
}

#endif


/*--------------------------------------------------------------------------
|	     Generalized routines (vector data type independent)
+-------------------------------------------------------------------------*/

#if defined (AVX512) || defined (FMA) || defined (AVX) || defined (SSE2) || !defined (X86_64)

void brute_line (
	CVDT	*invec1,		// First input poly (may be a monic with ones stripped)
	uint64_t invec1_size,		// Size of the first input polynomial
	CVDT	*invec2,		// Second input poly (may be a monic with ones stripped)
	uint64_t invec2_size,		// Size of the second input polynomial
	CVDT	*outvec,		// Output poly
	uint64_t circular_size,		// Output poly size (may be less than invec1_size + invec2_size - 1)
	gwnum	*gw_outvec,		// Gwnum array passed to polymult (so we can detect outputs that do not need to be computed)
	uint64_t gw_outvec_size,	// Size of the gwnum output vector (may be less than output poly size)
	int	adjusted_shift,		// Difference between base of outvec and base of true_outvec.  Can be non-zero when monic RLPs are multiplied.
	uint64_t LSWs_skipped)		// Least significant coefficients from true_outvec_size that do not need to be returned
{
	// Loop through output indices
	for (int out = 0; out < invec1_size + invec2_size - 1; out++) {
		// Adjust output index for circular convolutions
		int adjusted_out = out + adjusted_shift;
		bool first_time = TRUE;
		if (adjusted_out >= circular_size) {
			adjusted_out = adjusted_out % circular_size;
			first_time = FALSE;
		}
		// See if this output needs to be computed
		if (gw_outvec != NULL &&
		    (adjusted_out < LSWs_skipped || adjusted_out >= LSWs_skipped + gw_outvec_size || gw_outvec[adjusted_out - LSWs_skipped] == NULL)) continue;
		// Get index into input vectors
		uint64_t in1 = out < invec2_size ? 0 : out - invec2_size + 1;	// Index into invec1
		int64_t in2 = out - in1;					// Index into invec2
		// Loop through all the pairs to accumulate in this output
		for ( ; in1 < invec1_size && in2 >= 0; in1++, in2--) {
			CVDT tmp;
			cvmul (tmp, invec1[in1], invec2[in2]);
			if (first_time) {
				outvec[adjusted_out] = tmp;
				first_time = FALSE;
			} else {
				cvadd (outvec[adjusted_out], outvec[adjusted_out], tmp);
			}
		}
	}
}

// A recursive Karatsuba O(N^1.565) implementation of polynomial multiplication
//	a	b	(input vector 1)
//	c	d	(input vector 2)
//-----------------   
//ac   ad+bc   bd
//
//(a+b)*(c+d) = ac + ad + bc + bd
// ad+bc = (a+b)*(c+d) - ac - bd
void karatsuba_line (
	pmhandle *pmdata,		// Handle for polymult library
	CVDT	*invec1,		// Coefficients of first input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	CVDT	*invec2,		// Coefficients of second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	CVDT	*tmp,			// Temporary space
	CVDT	*outvec)		// Output poly coefficients
{
	uint64_t i, a_size, b_size, c_size, d_size, a_plus_b_size, c_plus_d_size, bd_size, ac_size, a_plus_b_times_c_plus_d_size;
	CVDT	*a, *b, *c, *d, *ac, *bd, *a_plus_b, *c_plus_d, *a_plus_b_times_c_plus_d;

	// Handle end of recursion
	if (invec1_size == 1 || invec2_size == 1 || invec1_size + invec2_size < pmdata->KARAT_BREAK) {
		brute_line (invec1, invec1_size, invec2, invec2_size, outvec, invec1_size + invec2_size - 1, NULL, 0, 0, 0);
		return;
	}

	// To simplify code, always make invec2 the larger poly.
	if (invec1_size > invec2_size) {
		uint64_t tmpsize = invec1_size; invec1_size = invec2_size; invec2_size = tmpsize;
		CVDT *tmpvec = invec1; invec1 = invec2; invec2 = tmpvec;
	}

	// Split larger vector roughly in half.  For readability, create pointers to two halves.
	d_size = (invec2_size + 1) / 2;
	c_size = invec2_size - d_size;
	c = invec2 + d_size, d = invec2;

	// Handle case where vectors are of wildly unequal lengths
	if (d_size >= invec1_size) {
		// Compute bd
		bd = outvec;
		karatsuba_line (pmdata, invec1, invec1_size, d, d_size, tmp, bd);
		bd_size = invec1_size + d_size - 1;
		// Compute bc and store it in tmp
		CVDT *bc = tmp;
		uint64_t bc_size = invec1_size + c_size - 1;
		tmp += bc_size;
		karatsuba_line (pmdata, invec1, invec1_size, c, c_size, tmp, bc);
		// add/copy bc to output
		for (i = 0; i < invec1_size - 1; i++) cvadd (outvec[i + d_size], outvec[i + d_size], bc[i]);
		for ( ; i < bc_size; i++) outvec[i + d_size] = bc[i];
		return;
	}

	// Split smaller vector, the size of the two lower halves must be the same.  For readability, create pointers to two halves.
	b_size = d_size;
	a_size = invec1_size - b_size;
	a = invec1 + b_size, b = invec1;

	// Compute (a+b)
	a_plus_b = outvec;
	for (i = 0; i < a_size && i < b_size; i++) cvadd (a_plus_b[i], a[i], b[i]);
//	for ( ; i < a_size; i++) a_plus_b[i] = a[i];		// In case a_size > b_size
	for ( ; i < b_size; i++) a_plus_b[i] = b[i];		// In case b_size > a_size
	a_plus_b_size = i;
	// Compute (c+d)
	c_plus_d = &outvec[i];
	for (i = 0; i < c_size && i < d_size; i++) cvadd (c_plus_d[i], c[i], d[i]);
//	for ( ; i < c_size; i++) c_plus_d[i] = c[i];		// In case c_size > d_size
	for ( ; i < d_size; i++) c_plus_d[i] = d[i];		// In case d_size > c_size
	c_plus_d_size = i;
	// Compute (a+b) * (c+d) and store it in tmp
	a_plus_b_times_c_plus_d = tmp;
	a_plus_b_times_c_plus_d_size = a_plus_b_size + c_plus_d_size - 1;
	tmp += a_plus_b_times_c_plus_d_size;
	karatsuba_line (pmdata, a_plus_b, a_plus_b_size, c_plus_d, c_plus_d_size, tmp, a_plus_b_times_c_plus_d);

	// Compute bd
	bd = outvec;
	karatsuba_line (pmdata, b, b_size, d, d_size, tmp, bd);
	bd_size = b_size + d_size - 1;
	// Output the complex value 0+0i
	memset (&outvec[bd_size], 0, sizeof (CVDT));
	// Compute ac
	ac = &outvec[bd_size+1];
	karatsuba_line (pmdata, a, a_size, c, c_size, tmp, ac);
	ac_size = a_size + c_size - 1;

	// subtract ac from middle
	for (i = 0; i < ac_size; i++) cvsub (a_plus_b_times_c_plus_d[i], a_plus_b_times_c_plus_d[i], ac[i]);
	// subtract bd from middle
	for (i = 0; i < bd_size; i++) cvsub (a_plus_b_times_c_plus_d[i], a_plus_b_times_c_plus_d[i], bd[i]);
	// add middle to output
	for (i = 0; i < a_plus_b_times_c_plus_d_size; i++) cvadd (outvec[i + b_size], outvec[i + b_size], a_plus_b_times_c_plus_d[i]);
}

// These routines will be compiled for every vector type except FMA (sometimes the FMA routines are the same as the AVX routines)

#ifndef FMA

// Read a line that was preprocessed by polymult_preprocess
void read_preprocess_line_slice (int helper_num, pmhandle *pmdata, void *rwlsd)
{
	gwhandle *gwdata = pmdata->gwdata;
	gwnum	*data = ((read_write_line_slice_data *) rwlsd)->data;
	uint64_t size = ((read_write_line_slice_data *) rwlsd)->size;
	int	line = ((read_write_line_slice_data *) rwlsd)->lsd.line;
	int	options = ((read_write_line_slice_data *) rwlsd)->options;
	bool	post_process_monics = ((read_write_line_slice_data *) rwlsd)->post_process_monics;

	preprocessed_poly_header *hdr;  // Header of the preprocessed poly being read
	CVDT	*first_element;		// First element in the preprocessed poly array
	CVDT	*element;		// Element to read in the preprocessed poly array
	uint64_t slice_size;		// Size of a slice to read

	// Typecast to access the preprocessed poly header
	hdr = (preprocessed_poly_header *) ((char *) data - sizeof (gwarray_header));
	first_element = (CVDT *) round_up_to_multiple_of ((intptr_t) hdr + sizeof (preprocessed_poly_header), 64);

	// Compute index into the preprocessed data.  If we've delayed splitting the two real values in !NEGACYCLIC_FFT then read the first array entry
	// for the first two indexes.  Finally, get pointer to the first byte to read.
	int index_to_read = line;
	if (!(hdr->options & POLYMULT_PRE_FFT) && !gwdata->NEGACYCLIC_FFT && !gwdata->ZERO_PADDED_FFT) {
		index_to_read--;
		if (index_to_read < 0) index_to_read = 0;
	}
	element = (CVDT *) ((char *) first_element + index_to_read * hdr->element_size);

	// If preprocessed data is FFTed, then ignore the passed in size, use the poly FFT size that was computed by polymult_line_preprocess
	if (hdr->options & POLYMULT_PRE_FFT) size = hdr->line_size;
	else ASSERTG (size == hdr->line_size);

	// If multi-threading read_preprocess_line, create a slice_size that will generate a goodly number of work units to multi-thread.
	// If compressed, we may be able to read the poly in slices of 512 doubles.
	if (pmdata->mt_polymult_line) slice_size = size;
	else if (!(hdr->options & POLYMULT_PRE_COMPRESS)) slice_size = divide_rounding_up (size, 512);
	else slice_size = hdr->line_size / hdr->num_compressed_blks;

	// Read one slice at a time
	for ( ; ; ) {
		uint64_t slice_start, slice_end;
		// Get slice to work on.  Atomics aren't cheap, avoid them if reading the whole line.
		if (slice_size == size) {
			slice_start = 0;
			slice_end = size;
		} else {
			int64_t atomic_val = atomic_fetch_incr (((read_write_line_slice_data *) rwlsd)->blknum);
			slice_start = atomic_val * slice_size;
			if (slice_start >= size) break;
			slice_end = slice_start + slice_size;
			if (slice_end > size) slice_end = size;
		}

		// Reload vector pointer
		CVDT *vec = ((read_write_line_slice_data *) rwlsd)->vec;

		// If preprocessed data is FFTed, then return the data "as is" optionally decompressing.  Ignore the passed in size, use the poly FFT size
		// that was computed by polymult_line_preprocess.
		if (hdr->options & POLYMULT_PRE_FFT) {
			if (!(hdr->options & POLYMULT_PRE_COMPRESS))
				memcpy (&vec[slice_start], &element[slice_start], (size_t) (slice_end - slice_start) * sizeof (CVDT));
			else
				decompress_line_slice ((char *) element, (char *) &vec[slice_start], hdr, slice_start, slice_end, sizeof (CVDT));
			if (slice_end == size) break;
			else continue;
		}

		// If not post-processing monics and the input is a monic RLP, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[0].real = broadcastsd (1.0);
					vec[0].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd;
					prep_line (pmdata->gwdata, line, &fft1_rwlsd);
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = vec;
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
			vec++;
		}

		// If input is an RLP then read the data into the high half of vec.  We will duplicate the data as the last step.
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) vec += (size-1);

		// Copy the preprocessed entry
		ASSERTG ((hdr->options & POLYMULT_PRE_COMPRESS) || hdr->element_size == size * sizeof (CVDT));
		if (!(hdr->options & POLYMULT_PRE_COMPRESS)) memcpy (&vec[slice_start], &element[slice_start], (size_t) (slice_end - slice_start) * sizeof (CVDT));
		else decompress_line_slice ((char *) element, (char *) &vec[slice_start], hdr, slice_start, slice_end, sizeof (CVDT));

		// If we've delayed splitting the two real values in !NEGACYCLIC_FFT turn the two reals into two complex values here
		if (!gwdata->NEGACYCLIC_FFT && !gwdata->ZERO_PADDED_FFT && index_to_read == 0) {
			for (uint64_t j = slice_start; j < slice_end; j++) {
				union { VDT a; double b[VLEN / sizeof(double)]; } r, i;
				r.a = vec[j].real;
				i.a = vec[j].imag;
				if (line == 0) {
					memset (&r, 0, sizeof (r));
					r.b[0] = i.b[0];
					memset (&i, 0, sizeof (i));
				} else {
					i.b[0] = 0.0;
				}
				vec[j].real = r.a;
				vec[j].imag = i.a;
			}
		}

		// Duplicate RLP data in reverse order
		if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)) {
			for (uint64_t j = slice_start; j < slice_end; j++) vec[-(int64_t)j] = vec[j];
		}

		// If not post-processing monics and the input is monic, then output a 1+0i
		if (!post_process_monics && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC))) {
			if (slice_start == 0) {
				if (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0) {
					vec[size].real = broadcastsd (1.0);
					vec[size].imag = broadcastsd (0.0);
				} else {
					read_write_line_slice_data fft1_rwlsd;
					prep_line (pmdata->gwdata, line, &fft1_rwlsd);
					fft1_rwlsd.data = &gwdata->GW_FFT1;
					fft1_rwlsd.size = 1;
					fft1_rwlsd.vec = &vec[size];
					fft1_rwlsd.options = 0;
					fft1_rwlsd.post_process_monics = FALSE;
					read_line (pmdata, &fft1_rwlsd);
				}
			}
		}

		// Check for completion
		if (slice_end == size) break;
	}
}

void read_line (pmhandle *pmdata, read_write_line_slice_data *rwlsd)
{
	// Launch helpers to read a line in slices.  This is tricky.  There are two cases.  The most common case is polymult_line is multi-threaded
	// and read_line_slice is single-threaded.  The less common case is polymult_line is single-threaded and read_line_slice is multi-threaded.
	// If polymult_line is multi-threaded we cannot set pmdata->helper_opcode as all helper threads may not have dispatched based on the opcode,
	// nor can we have each thread setting pmdata->internal_callback_data.  To handle this we call read_line_slice directly when read_line_slice is
	// single-threaded.  When read_line_slice is multi-threaded we use all the polymult_launch machinery, but polymult_launch must not
	// atomic_set helper_counter as polymult_line is using that variable.
	if (!is_preprocessed_poly (rwlsd->data)) {
		if (pmdata->mt_polymult_line || rwlsd->size < 512) {
			rwlsd->slice_size = rwlsd->size;
			read_line_slice (0, pmdata, rwlsd);
		} else {
			// Create a slice_size that will generate a goodly number of work units to multi-thread
			rwlsd->slice_size = divide_rounding_up (rwlsd->size, 512);
			atomic_set (rwlsd->blknum, 0);
			pmdata->internal_callback = &read_line_slice;
			pmdata->internal_callback_data = rwlsd;
			polymult_launch_helpers (pmdata);
		}
	}
	// Launch helpers to read a preprocessed line in slices.  This is tricky.  There are two cases.  The most common case is polymult_line is multi-threaded and
	// read_preprocess_line_slice is single-threaded.  The less common case is polymult_line is single-threaded and read_preprocess_line_slice is multi-threaded.
	// If polymult_line is multi-threaded we cannot set pmdata->helper_opcode as all helper threads may not have dispatched based on the opcode,
	// nor can we have each thread setting pmdata->internal_callback_data.  To handle this we call read_preprocess_line_slice directly when
	// read_preprocess_line_slice is single-threaded.  When read_preprocess_line_slice is multi-threaded we use all the polymult_launch machinery, but
	// polymult_launch must not atomic_set helper_counter as polymult_line is using that variable.
	else {
		if (pmdata->mt_polymult_line) {
			read_preprocess_line_slice (0, pmdata, rwlsd);
		} else {
			atomic_set (rwlsd->blknum, 0);
			pmdata->internal_callback = &read_preprocess_line_slice;
			pmdata->internal_callback_data = rwlsd;
			polymult_launch_helpers (pmdata);
		}
	}
}

void write_line (pmhandle *pmdata, read_write_line_slice_data *rwlsd)
{
	// Launch helpers to do write a line in slices.  This is tricky.  There are two cases.  The most common case is polymult_line is multi-threaded
	// and write_line_slice is single-threaded.  The less common case is polymult_line is single-threaded and write_line_slice is multi-threaded.
	// If polymult_line is multi-threaded we cannot set pmdata->helper_opcode as all helper threads may not have dispatched based on the opcode,
	// nor can we have each thread setting pmdata->internal_callback_data.  To handle this we call write_line_slice directly when write_line_slice is
	// single-threaded.  When write_line_slice is multi-threaded we use all the polymult_launch machinery, but polymult_launch must not
	// atomic_set helper_counter as polymult_line is using that variable.
	if (pmdata->mt_polymult_line) {
		rwlsd->slice_size = rwlsd->size;
		write_line_slice (0, pmdata, rwlsd);
	} else {
		// Create a slice_size that will generate a goodly number of work units to multi-thread
		rwlsd->slice_size = divide_rounding_up (rwlsd->size, 512);
		atomic_set (rwlsd->blknum, 0);
		pmdata->internal_callback = &write_line_slice;
		pmdata->internal_callback_data = rwlsd;
		polymult_launch_helpers (pmdata);
	}
}

#endif

// The fft_line can be called several different options:
#define	FORWARD_FFT_INVEC1	0x1	// Forward FFT invec1 required
#define	FORWARD_FFT_INVEC2	0x2	// Forward FFT invec2 required
#define FORWARD_FFT_ONLY	0x4	// No pointwise multiply and inverse FFT

// Structure used to pass data from fft_line to fft_line_pass
typedef struct {
	read_write_line_slice_data *rwlsd1;	// Info as to where invec1 is or how to read invec1
	read_write_line_slice_data *rwlsd2;	// Info as to where invec2 is or how to read invec2
	read_write_line_slice_data *rwlsd3;	// Info as to where outvec is or how to write outvec
	CVDT	*invec1;			// Where to write the FFT of invec1
	CVDT	*invec2;			// Where to write the FFT of invec2
	CVDT	*outvec;			// Where to write the result of invec1 * invec2
	CVDT	*scratch;			// Scratch area for two-pass FFTs
	int	options;			// Options passed to fft_line
	bool	multithreading;			// TRUE if multi-threading each FFT pass
	bool	write_direct;			// TRUE if FFT should copy outvec to the rwlsd3 gwnum array
	uint64_t invec1_size;			// Size of invec1 after read_line.  Will zero pad from invec1_size to fft_size.
	uint64_t invec2_size;			// Size of invec2 after read_line.  Will zero pad from invec2_size to fft_size.
	uint64_t fft_size;			// FFT size
	unsigned int powers_of_two;		// Count of powers of two in fft_size
	uint32_t pass1_size;			// Size of pass 1 in a two-pass FFT
	uint32_t pass2_size;			// Size of pass 2 in a two-pass FFT
	uint64_t starting_twiddle3_stride;	// Starting stride in a fft_line_pass for the radix-3 twiddles data
	uint64_t starting_twiddle45_stride;	// Starting stride in a fft_line_pass for the radix-4/5 twiddles data
	unsigned int pass;			// Pass number (1,2,3).  The pointwise multiplication takes place in pass 2.
	unsigned int pass_size;			// Size of the current pass
	unsigned int pass_stride;		// Distance between smallest elements in a pass
	unsigned int num_exterior_blks;		// Num blocks larger than pass_stride (for 2-pass FFTs this equals 1 or pass1_size)
	unsigned int num_interior_blks;		// Num blocks smaller than pass_stride (for 2-pass FFTs this equals pass2_size or 1)
	VDT	inv_fft_size;			// 1 / fft_size
	VDT	inv_fft_size_707;		// 1 / fft_size * sqrt(0.5)
	gwatomic blknum;			// Block number to work on in multi-threaded fft_line_pass
} fft_line_pass_data;

// Perform one pass of fft_line

void fft_line_pass (
	int	helper_num,		// Helper number (main thread is 0, launched threads are 1+)
	pmhandle *pmdata,		// Handle for polymult library
	void	*pdarg)			// fft_line_pass_data
{
	fft_line_pass_data *pd = (fft_line_pass_data *) pdarg;	// Data passed to us from fft_line
	CVDT	*scratch;		// Each helper thread will need its own scratch buffer
	int	non_atomic_val = 0;	// Counter if not using atomics due to multithreading

	// Calculate the scratch address
	scratch = pd->scratch + helper_num * pd->pass1_size;

	// Execute one FFT pass a block at a time
	for ( ; ; ) {
		unsigned int blknum;

		// Get blknum to work on either atomicly or non-atomicly
		if (pd->multithreading) blknum = (int) atomic_fetch_incr (pd->blknum);
		else blknum = non_atomic_val++;
		if (blknum >= pd->num_exterior_blks * pd->num_interior_blks) break;

		// Convert atomic_val (blknum) into exterior block# and interior block#
#ifdef THREE_PASS_FFTS_SUPPORTED_SOMEDAY
		unsigned int blke = blknum / pd->num_exterior_blks;			// Exterior block number
		unsigned int blki = blknum - blke * pd->num_exterior_blks;		// Interior block number
#else
		unsigned int blke = (pd->num_exterior_blks == 1 ? 0 : blknum);		// Exterior block number
		unsigned int blki = (pd->num_exterior_blks == 1 ? blknum : 0);		// Interior block number
#endif

		CVDT *src;				// FFT source data
		CVDT *dest;				// FFT destination data
		unsigned int size;			// The current group size to process
		unsigned int stride;			// Logical stride through the FFT (often the same as input and output stride through memory)
		unsigned int instride1;			// Unit stride through input FFT data in memory
		unsigned int outstride1;		// Unit stride through output FFT data in memory
		uint64_t twiddle3_stride;		// Stride through the radix-3 twiddles data
		uint64_t twiddle45_stride;		// Stride through the radix-4/5 twiddles data
		uint64_t src_size;			// How much of the pass_size has been read in for this blknum (rest of pass_size must be zero padded)
		bool	zpad;				// TRUE if zero padding is needed

// Load starting twiddle strides for each block in the pass

		twiddle3_stride = pd->starting_twiddle3_stride;
		twiddle45_stride = pd->starting_twiddle45_stride;

// Forward FFT.  Loop over both input vectors.  Both may need a forward FFT.

		if (pd->pass <= 2) {
		    for (int srcarg = 1; srcarg <= 2; srcarg++) {

			// Skip input vectors that do not need a forward FFT
			if (srcarg == 1 && !(pd->options & FORWARD_FFT_INVEC1)) continue;
			if (srcarg == 2 && !(pd->options & FORWARD_FFT_INVEC2)) continue;

			// Pass 1 reads from an input vector into a scratch area to reduce large strides which CPU caches hate.  At end, result written
			// back to input vector.  Pass 2 reads/writes directly from/to an input vector.
			if (pd->pass == 1) {
//GW Use where rwlsd read into as the source		(until we come up with comprehensive sourcing procedures)
src = (srcarg == 1 ? pd->rwlsd1->vec : pd->rwlsd2->vec) + blki;
//was:	src = (srcarg == 1 ? pd->invec1 : pd->invec2) + blki;
				dest = scratch;
				instride1 = pd->pass2_size;
				outstride1 = 1;
				src_size = divide_rounding_up ((srcarg == 1 ? pd->invec1_size : pd->invec2_size) - blknum, instride1);
				zpad = (src_size != pd->pass_size);
			}
			else {
//GW Use where rwlsd read into as the source
if (pd->pass1_size == 1) src = (srcarg == 1 ? pd->rwlsd1->vec : pd->rwlsd2->vec), dest = (srcarg == 1 ? pd->invec1 : pd->invec2);
else src = dest = (srcarg == 1 ? pd->invec1 : pd->invec2) + blke * pd->pass2_size;
				instride1 = outstride1 = 1;
				src_size = (srcarg == 1 ? pd->invec1_size : pd->invec2_size);
				zpad = (pd->pass1_size == 1 && src_size != pd->pass_size);
			}

			// Reset twiddle strides for each input vector
			twiddle3_stride = pd->starting_twiddle3_stride;
			twiddle45_stride = pd->starting_twiddle45_stride;

			// Forward FFT.  Start working with group size as the whole pass size, then work on progressively smaller group sizes.
			size = pd->pass_size;

			// Radix-3 FFT
			// A 3-complex FFT is:
			// Res1:  (R1+R2+R3) + (I1+I2+I3)i
			// Res2:  (R1-.5R2-.5R3-.866I2+.866I3) + (I1-.5I2-.5I3+.866R2-.866R3)i
			// Res3:  (R1-.5R2-.5R3+.866I2-.866I3) + (I1-.5I2-.5I3-.866R2+.866R3)i
			for ( ; size % 3 == 0; size /= 3, twiddle3_stride *= 3, src = dest, instride1 = 1, zpad = FALSE) {
			    VDT half = broadcastsd (0.5);
			    VDT _866 = broadcastsd (0.86602540378443864676372317075294);
			    stride = size / 3;
			    if (pd->pass == 1 && stride == 1) dest = (srcarg == 1 ? pd->invec1 : pd->invec2) + blki, outstride1 = pd->pass2_size;
			    for (unsigned int group = 0; group < pd->pass_size; group += stride * 3) {
				uint64_t group_member = 0;		// Current group member to process
				uint64_t last_group_member;		// Last group member to process.  We will process stride group members.

				// Radix-3 with no zero padding
				if (!zpad) last_group_member = stride;			// The not zero padding case, read 3 values for all groups
				else if (src_size <= 2 * stride) last_group_member = 0;	// No groups have 3 values to read
				else last_group_member = src_size - 2 * stride;		// Some groups have 3 values, the rest have 2 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle3_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];
					CVDT b_plus_c, b_minus_c, tmp23;

					cvaddsub (b_plus_c, b_minus_c, b, c);			// b + c, b - c
					cvsubfm (tmp23, a, half, b_plus_c);			// tmp23 = a - 0.5 * (b + c)

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsubfm (b, c, tmp23, _866, b_minus_c);	// b = tmp23 + i * .866 * (b - c), c = tmp23 - i * .866 * (b - c)
						cvadd (a, a, b_plus_c);				// a + b + c
						dest[outstride1*(group + group_member)] = a;
						dest[outstride1*(group + group_member + stride)] = b;
						dest[outstride1*(group + group_member + 2*stride)] = c;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles1[2*twiddle_idx]);
						VDT sin866 = mulpd (_866, twiddle1.sin);
						cvrmul (tmp23, twiddle1.sin, tmp23);
						cvadd (a, a, b_plus_c);				// a + b + c
						cviaddsubfm (b, c, tmp23, sin866, b_minus_c);	// b = tmp23 + i * .866 * (b - c), c = tmp23 - i * .866 * (b - c)
						dest[outstride1*(group + group_member)] = a;
						twidmuldelay     (dest[outstride1*(group + group_member + stride)], b, twiddle1);
						twidconjmuldelay (dest[outstride1*(group + group_member + 2*stride)], c, twiddle1);
					}
				}
				if (group_member == stride) continue;

				// Radix-3 reading two values and zero padding one value.  NOTE: group must be zero.
				ASSERTG (group == 0);  ASSERTG (outstride1 == 1); ASSERTG (instride1 == pd->pass_stride); ASSERTG (twiddle3_stride == 1);
				if (group_member != 0) last_group_member = stride;	// We found some 3 value groups, the rest have 2 values
				else if (src_size <= stride) last_group_member = 0;	// No groups have 2 values to read
				else last_group_member = src_size - stride;		// Some groups have 2 values, the rest have 1 value
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];
					CVDT b_in = src[instride1*(group_member + stride)];
					CVDT b, c, tmp23;

					cvsubfm (tmp23, a, half, b_in);				// tmp23 = a - 0.5 * (b + c=0)

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cvadd (a, a, b_in);				// a + (b + c=0)
						cviaddsubfm (b, c, tmp23, _866, b_in);		// b = tmp23 + i * .866 * (b - c=0), c = tmp23 - i * .866 * (b - c=0)
						dest[group_member] = a;
						dest[group_member + stride] = b;
						dest[group_member + 2*stride] = c;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles1[2*twiddle_idx]);
						VDT sin866 = mulpd (_866, twiddle1.sin);
						cvrmul (tmp23, twiddle1.sin, tmp23);
						cvadd (a, a, b_in);				// a + b + c=0
						cviaddsubfm (b, c, tmp23, sin866, b_in);	// b = tmp23 + i * .866 * (b - c=0), c = tmp23 - i * .866 * (b - c=0)
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + stride], b, twiddle1);
						twidconjmuldelay (dest[group_member + 2*stride], c, twiddle1);
					}
				}

				// Radix-3 reading one value and zero padding two values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 2 value groups, the rest have 1 value
				else last_group_member = src_size;			// Some groups have 1 values, the rest have 0 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						dest[group_member] = a;
						dest[group_member + stride] = a;
						dest[group_member + 2*stride] = a;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles1[2*twiddle_idx]);
						dest[group_member] = a;
						twidmulandconjmul (dest[group_member + stride], dest[group_member + 2*stride], a, twiddle1);
					}
				}

				// Radix-3 reading zero values and zero padding three values.  NOTE: group must be zero.
				for ( ; group_member < stride; group_member++) {
					memset (&dest[group_member], 0, sizeof (CVDT));
					memset (&dest[group_member + stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 2*stride], 0, sizeof (CVDT));
				}
			    }
			}

			// Radix-5 FFT
			// R1= r1     +(r2+r5)     +(r3+r4)
			// R2= r1 +.309(r2+r5) -.809(r3+r4)    -.951(i2-i5) -.588(i3-i4)
			// R5= r1 +.309(r2+r5) -.809(r3+r4)    +.951(i2-i5) +.588(i3-i4)
			// R3= r1 -.809(r2+r5) +.309(r3+r4)    -.588(i2-i5) +.951(i3-i4)
			// R4= r1 -.809(r2+r5) +.309(r3+r4)    +.588(i2-i5) -.951(i3-i4)
			// I1= i1     +(i2+i5)     +(i3+i4)
			// I2= i1 +.309(i2+i5) -.809(i3+i4)    +.951(r2-r5) +.588(r3-r4)
			// I5= i1 +.309(i2+i5) -.809(i3+i4)    -.951(r2-r5) -.588(r3-r4)
			// I3= i1 -.809(i2+i5) +.309(i3+i4)    +.588(r2-r5) -.951(r3-r4)
			// I4= i1 -.809(i2+i5) +.309(i3+i4)    -.588(r2-r5) +.951(r3-r4)
			for ( ; size % 5 == 0; size /= 5, twiddle45_stride *= 5, src = dest, instride1 = 1, zpad = FALSE) {
			    VDT _309 = broadcastsd (0.30901699437494742410229341718282);
			    VDT _809 = broadcastsd (0.80901699437494742410229341718282);
			    VDT _951 = broadcastsd (0.95105651629515357211643933337938);
			    VDT _588_951 = broadcastsd (0.61803398874989484820458683436564);
			    VDT _m588 = broadcastsd (-0.58778525229247312916870595463907);
			    stride = size / 5;
			    if (pd->pass == 1 && stride == 1) dest = (srcarg == 1 ? pd->invec1 : pd->invec2) + blki, outstride1 = pd->pass2_size;
			    for (unsigned int group = 0; group < pd->pass_size; group += stride * 5) {
				uint64_t group_member = 0;		// Current group member to process
				uint64_t last_group_member;		// Last group member to process.  We will process stride group members.

				// Radix-5 with no zero padding
				if (!zpad) last_group_member = stride;			// The not zero padding case, read 5 values for all groups
				else if (src_size <= 4 * stride) last_group_member = 0;	// No groups have 5 values to read
				else last_group_member = src_size - 4 * stride;		// Some groups have 5 values, the rest have 4 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle45_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];
					CVDT d = src[instride1*(group + group_member + 3*stride)];
					CVDT e = src[instride1*(group + group_member + 4*stride)];

					CVDT b_plus_e, b_minus_e, c_plus_d, c_minus_d, tmp25a, tmp25b, tmp34a, tmp34b;

					cvadd (b_plus_e, b, e);					// b + e
					cvsub (b_minus_e, b, e);				// b - e
					cvadd (c_plus_d, c, d);					// c + d
					cvsub (c_minus_d, c, d);				// c - d

					cvaddfm (tmp25a, a, _309, b_plus_e);
					cvsubfm (tmp34a, a, _809, b_plus_e);
					cvadd   (a, a, b_plus_e);
					cvaddfm (tmp25b, b_minus_e, _588_951, c_minus_d);	// tmp25b = delayed .951*(b-e) + 0.588*(c-d)
					cvsubfm (tmp34b, c_minus_d, _588_951, b_minus_e);	// tmp34b = delayed -.588*(b-e) + 0.951*(c-d)
					cvsubfm (tmp25a, tmp25a, _809, c_plus_d);		// tmp25a = a +.309*(b+e) -.809*(c+d)
					cvaddfm (tmp34a, tmp34a, _309, c_plus_d);		// tmp34a = a -.809*(b+e) +.309*(c+d)
					cvadd   (a, a, c_plus_d);				// a + b + c + d + e

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsubfm (b, e, tmp25a, _951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, _951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[outstride1*(group + group_member)] = a;
						dest[outstride1*(group + group_member + stride)]   = b;
						dest[outstride1*(group + group_member + 2*stride)] = c;
						dest[outstride1*(group + group_member + 3*stride)] = d;
						dest[outstride1*(group + group_member + 4*stride)] = e;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						VDT sin1_951 = mulpd (_951, twiddle1.sin);
						VDT sin2_951 = mulpd (_951, twiddle2.sin);
						cvrmul   (tmp25a, twiddle1.sin, tmp25a);
						cvrmul   (tmp34a, twiddle2.sin, tmp34a);
						cviaddsubfm (b, e, tmp25a, sin1_951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, sin2_951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[outstride1*(group + group_member)] = a;
						twidmuldelay     (dest[outstride1*(group + group_member + stride)], b, twiddle1);
						twidmuldelay     (dest[outstride1*(group + group_member + 2*stride)], c, twiddle2);
						twidconjmuldelay (dest[outstride1*(group + group_member + 3*stride)], d, twiddle2);
						twidconjmuldelay (dest[outstride1*(group + group_member + 4*stride)], e, twiddle1);
					}
				}
				if (group_member == stride) continue;

				// Radix-5 reading four values and zero padding one value.  NOTE: group must be zero.
				ASSERTG (group == 0);  ASSERTG (outstride1 == 1); ASSERTG (instride1 == pd->pass_stride); ASSERTG (twiddle45_stride == 1);
				if (group_member != 0) last_group_member = stride;	// We found some 5 value groups, the rest have 4 values
				else if (src_size <= 3 * stride) last_group_member = 0;	// No groups have 4 values to read
				else last_group_member = src_size - 3 * stride;		// Some groups have 4 values, the rest have 3 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];
					CVDT b = src[instride1*(group_member + stride)];
					CVDT c = src[instride1*(group_member + 2*stride)];
					CVDT d = src[instride1*(group_member + 3*stride)];
					CVDT e, c_plus_d, c_minus_d, tmp25a, tmp25b, tmp34a, tmp34b;

					cvadd (c_plus_d, c, d);					// c + d
					cvsub (c_minus_d, c, d);				// c - d

					cvaddfm (tmp25a, a, _309, b);
					cvsubfm (tmp34a, a, _809, b);
					cvadd   (a, a, b);
					cvaddfm (tmp25b, b, _588_951, c_minus_d);		// tmp25b = delayed .951*(b-e=0) + 0.588*(c-d)
					cvsubfm (tmp34b, c_minus_d, _588_951, b);		// tmp34b = delayed -.588*(b-e=0) + 0.951*(c-d)
					cvsubfm (tmp25a, tmp25a, _809, c_plus_d);		// tmp25a = a +.309*(b+e=0) -.809*(c+d)
					cvaddfm (tmp34a, tmp34a, _309, c_plus_d);		// tmp34a = a -.809*(b+e=0) +.309*(c+d)
					cvadd   (a, a, c_plus_d);				// a + b + c + d + e

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsubfm (b, e, tmp25a, _951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, _951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[group_member] = a;
						dest[group_member + stride]   = b;
						dest[group_member + 2*stride] = c;
						dest[group_member + 3*stride] = d;
						dest[group_member + 4*stride] = e;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						VDT sin1_951 = mulpd (_951, twiddle1.sin);
						VDT sin2_951 = mulpd (_951, twiddle2.sin);
						cvrmul   (tmp25a, twiddle1.sin, tmp25a);
						cvrmul   (tmp34a, twiddle2.sin, tmp34a);
						cviaddsubfm (b, e, tmp25a, sin1_951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, sin2_951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + stride], b, twiddle1);
						twidmuldelay     (dest[group_member + 2*stride], c, twiddle2);
						twidconjmuldelay (dest[group_member + 3*stride], d, twiddle2);
						twidconjmuldelay (dest[group_member + 4*stride], e, twiddle1);
					}
				}

				// Radix-5 reading three values and zero padding two values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 4 value groups, the rest have 3 values
				else if (src_size <= 2 * stride) last_group_member = 0;	// No groups have 3 values to read
				else last_group_member = src_size - 2 * stride;		// Some groups have 3 values, the rest have 2 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];
					CVDT b = src[instride1*(group_member + stride)];
					CVDT c = src[instride1*(group_member + 2*stride)];
					CVDT d, e, tmp25a, tmp25b, tmp34a, tmp34b;

					cvaddfm (tmp25a, a, _309, b);
					cvsubfm (tmp34a, a, _809, b);
					cvadd   (a, a, b);
					cvaddfm (tmp25b, b, _588_951, c);			// tmp25b = delayed .951*(b-e=0) + 0.588*(c-d=0)
					cvsubfm (tmp34b, c, _588_951, b);			// tmp34b = delayed -.588*(b-e=0) + 0.951*(c-d=0)
					cvsubfm (tmp25a, tmp25a, _809, c);			// tmp25a = a +.309*(b+e=0) -.809*(c+d=0)
					cvaddfm (tmp34a, tmp34a, _309, c);			// tmp34a = a -.809*(b+e=0) +.309*(c+d=0)
					cvadd   (a, a, c);					// a + b + c + d=0 + e=0

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsubfm (b, e, tmp25a, _951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, _951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[group_member] = a;
						dest[group_member + stride]   = b;
						dest[group_member + 2*stride] = c;
						dest[group_member + 3*stride] = d;
						dest[group_member + 4*stride] = e;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						VDT sin1_951 = mulpd (_951, twiddle1.sin);
						VDT sin2_951 = mulpd (_951, twiddle2.sin);
						cvrmul   (tmp25a, twiddle1.sin, tmp25a);
						cvrmul   (tmp34a, twiddle2.sin, tmp34a);
						cviaddsubfm (b, e, tmp25a, sin1_951, tmp25b);	// b = tmp25a + i*.951*tmp25b, e = tmp25a - i*.951*tmp25b
						cvisubaddfm (c, d, tmp34a, sin2_951, tmp34b);	// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + stride], b, twiddle1);
						twidmuldelay     (dest[group_member + 2*stride], c, twiddle2);
						twidconjmuldelay (dest[group_member + 3*stride], d, twiddle2);
						twidconjmuldelay (dest[group_member + 4*stride], e, twiddle1);
					}
				}

				// Radix-5 reading two values and zero padding three values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 3 value groups, the rest have 2 values
				else if (src_size <= stride) last_group_member = 0;	// No groups have 2 values to read
				else last_group_member = src_size - stride;		// Some groups have 2 values, the rest have 1 value
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];
					CVDT b_in = src[instride1*(group_member + stride)];
					CVDT b, c, d, e, tmp25a, tmp34a;

					cvaddfm (tmp25a, a, _309, b_in);			// tmp25a = a +.309*(b+e=0) -.809*(c=0+d=0)
					cvsubfm (tmp34a, a, _809, b_in);			// tmp34a = a -.809*(b+e=0) +.309*(c=0+d=0)
					cvadd   (a, a, b_in);					// a = a + b + c=0 + d=0 + e=0

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsubfm (b, e, tmp25a, _951, b_in);		// b = tmp25a + i*.951*b, e = tmp25a - i*.951*b
						cvisubaddfm (c, d, tmp34a, _m588, b_in);	// c = tmp34a + i*-.588*b, d = tmp34a - i*-.588*b
						dest[group_member] = a;
						dest[group_member + stride]   = b;
						dest[group_member + 2*stride] = c;
						dest[group_member + 3*stride] = d;
						dest[group_member + 4*stride] = e;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						VDT sin1_951 = mulpd (_951, twiddle1.sin);
						VDT sin2_m588 = mulpd (_m588, twiddle2.sin);
						cvrmul   (tmp25a, twiddle1.sin, tmp25a);
						cvrmul   (tmp34a, twiddle2.sin, tmp34a);
						cviaddsubfm (b, e, tmp25a, sin1_951, b_in);	// b = tmp25a + i*.951*b, e = tmp25a - i*.951*b
						cvisubaddfm (c, d, tmp34a, sin2_m588, b_in);	// c = tmp34a + i*-.588*b, d = tmp34a - i*-.588*b
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + stride], b, twiddle1);
						twidmuldelay     (dest[group_member + 2*stride], c, twiddle2);
						twidconjmuldelay (dest[group_member + 3*stride], d, twiddle2);
						twidconjmuldelay (dest[group_member + 4*stride], e, twiddle1);
					}
				}

				// Radix-5 reading one values and zero padding four values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 2 value groups, the rest have 1 value
				else last_group_member = src_size;			// Some groups have 1 value, the rest have 0 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						dest[group_member] = a;
						dest[group_member + stride] = a;
						dest[group_member + 2*stride] = a;
						dest[group_member + 3*stride] = a;
						dest[group_member + 4*stride] = a;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						dest[group_member] = a;
						twidmulandconjmul (dest[group_member + stride], dest[group_member + 4*stride], a, twiddle1);
						twidmulandconjmul (dest[group_member + 2*stride], dest[group_member + 3*stride], a, twiddle2);
					}
				}

				// Radix-5 reading zero values and zero padding five values.  NOTE: group must be zero.
				for ( ; group_member < stride; group_member++) {
					memset (&dest[group_member], 0, sizeof (CVDT));
					memset (&dest[group_member + stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 2*stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 3*stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 4*stride], 0, sizeof (CVDT));
				}
			    }
			}

			// Radix-4 FFT
			for ( ; size != 8 && size % 4 == 0; size /= 4, twiddle45_stride *= 4, src = dest, instride1 = 1, zpad = FALSE) {
			    stride = size / 4;
			    if (pd->pass == 1 && stride == 1) dest = (srcarg == 1 ? pd->invec1 : pd->invec2) + blki, outstride1 = pd->pass2_size;
			    for (unsigned int group = 0; group < pd->pass_size; group += stride * 4) {
				uint64_t group_member = 0;		// Current group member to process
				uint64_t last_group_member;		// Last group member to process.  We will process stride group members.

				// Radix-4 with no zero padding
				if (!zpad) last_group_member = stride;			// The not zero padding case, read 4 values for all groups
				else if (src_size <= 3 * stride) last_group_member = 0;	// No groups have 4 values to read
				else last_group_member = src_size - 3 * stride;		// Some groups have 3 values, the rest have 3 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle45_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];
					CVDT d = src[instride1*(group + group_member + 3*stride)];
					CVDT new_a, new_b, new_c, new_d;

					cvaddsub (new_a, new_c, a, c);				// a + c, a - c
					cvaddsub (new_b, new_d, b, d);				// b + d, b - d

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cvaddsub (a, b, new_a, new_b);			// a + b, a - b
						cviaddsub (c, d, new_c, new_d);			// c + id, c - id
						dest[outstride1*(group + group_member)] = a;
						dest[outstride1*(group + group_member + 1*stride)] = c;		// Do not bit-reverse outputs
						dest[outstride1*(group + group_member + 2*stride)] = b;
						dest[outstride1*(group + group_member + 3*stride)] = d;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						cvrmul   (new_c, twiddle1.sin, new_c);
						cvaddsub (a, b, new_a, new_b);			// a + b, a - b
						cviaddsubfm (c, d, new_c, twiddle1.sin, new_d);	// c + id, c - id
						dest[outstride1*(group + group_member)] = a;
						twidmuldelay     (dest[outstride1*(group + group_member + 1*stride)], c, twiddle1);
						twidmul          (dest[outstride1*(group + group_member + 2*stride)], b, twiddle2);
						twidconjmuldelay (dest[outstride1*(group + group_member + 3*stride)], d, twiddle1);
					}
				}
				if (group_member == stride) continue;

				// Radix-4 reading three values and zero padding one value.  NOTE: group must be zero.
				ASSERTG (group == 0);  ASSERTG (outstride1 == 1); ASSERTG (instride1 == pd->pass_stride); ASSERTG (twiddle45_stride == 1);
				if (group_member != 0) last_group_member = stride;	// We found some 4 value groups, the rest have 3 values
				else if (src_size <= 2 * stride) last_group_member = 0;	// No groups have 3 values to read
				else last_group_member = src_size - 2 * stride;		// Some groups have 3 values, the rest have 2 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];
					CVDT b_in = src[instride1*(group_member + stride)];
					CVDT c = src[instride1*(group_member + 2*stride)];
					CVDT b, d, new_a, new_c;

					cvaddsub (new_a, new_c, a, c);				// a + c, a - c

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cviaddsub (c, d, new_c, b_in);			// c + id, c - id
						cvaddsub (a, b, new_a, b_in);			// a + b, a - b
						dest[group_member] = a;
						dest[group_member + 1*stride] = c;		// Do not bit-reverse outputs
						dest[group_member + 2*stride] = b;
						dest[group_member + 3*stride] = d;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						cvrmul   (new_c, twiddle1.sin, new_c);
						cvaddsub (a, b, new_a, b_in);			// a + b, a - b
						cviaddsubfm (c, d, new_c, twiddle1.sin, b_in);	// c + id, c - id
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + 1*stride], c, twiddle1);
						twidmul          (dest[group_member + 2*stride], b, twiddle2);
						twidconjmuldelay (dest[group_member + 3*stride], d, twiddle1);
					}
				}

				// Radix-4 reading two values and zero padding two values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 3 value groups, the rest have 2 values
				else if (src_size <= stride) last_group_member = 0;	// No groups have 2 values to read
				else last_group_member = src_size - stride;		// Some groups have 2 values, the rest have 1 value
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a_in = src[instride1*(group_member)];
					CVDT b_in = src[instride1*(group_member + stride)];
					CVDT a, b, c, d;

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						cvaddsub (a, b, a_in, b_in);			// a + b, a - b
						cviaddsub (c, d, a_in, b_in);			// c + id, c - id
						dest[group_member] = a;
						dest[group_member + 1*stride] = c;		// Do not bit-reverse outputs
						dest[group_member + 2*stride] = b;
						dest[group_member + 3*stride] = d;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						CVDT new_c;
						cvrmul   (new_c, twiddle1.sin, a_in);
						cvaddsub (a, b, a_in, b_in);			// a + b, a - b
						cviaddsubfm (c, d, new_c, twiddle1.sin, b_in);	// c + id, c - id
						dest[group_member] = a;
						twidmuldelay     (dest[group_member + 1*stride], c, twiddle1);
						twidmul          (dest[group_member + 2*stride], b, twiddle2);
						twidconjmuldelay (dest[group_member + 3*stride], d, twiddle1);
					}
				}

				// Radix-4 reading one values and zero padding three values.  NOTE: group must be zero.
				if (group_member != 0) last_group_member = stride;	// We found some 2 value groups, the rest have 1 value
				else last_group_member = src_size;			// Some groups have 1 value, the rest have 0 values
				for ( ; group_member < last_group_member; group_member++) {
					uint64_t twiddle_idx = group_member * instride1 + blki;
					CVDT a = src[instride1*(group_member)];

					if (twiddle_idx == 0) {	// First group member's twiddles are 1+0i, skip twiddle multiply
						dest[group_member] = a;
						dest[group_member + 1*stride] = a;
						dest[group_member + 2*stride] = a;
						dest[group_member + 3*stride] = a;
					}
					else {			// Apply twiddles
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						dest[group_member] = a;
						twidmulandconjmul (dest[group_member + 1*stride], dest[group_member + 3*stride], a, twiddle1);
						twidmul           (dest[group_member + 2*stride], a, twiddle2);
					}
				}

				// Radix-4 reading zero values and zero padding four values.  NOTE: group must be zero.
				for ( ; group_member < stride; group_member++) {
					memset (&dest[group_member], 0, sizeof (CVDT));
					memset (&dest[group_member + stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 2*stride], 0, sizeof (CVDT));
					memset (&dest[group_member + 3*stride], 0, sizeof (CVDT));
				}
			    }
			}

			// Radix-8 FFT (if needed it is the last radix block done in pass 2)
			ASSERTG (!zpad);
			if (size == 8) {
			    VDT _707 = broadcastsd (0.70710678118654752440084436210485);
			    stride = 1;
			    for (unsigned int group = 0; group < pd->pass_size; group += stride * 8) {
//GW: The group_member loop is not needed
//GW: Cost out radix-8 vs radix-4
				for (unsigned int group_member = 0; group_member < stride; group_member++) {
					CVDT a = src[group + group_member];
					CVDT b = src[group + group_member + stride];
					CVDT c = src[group + group_member + 2*stride];
					CVDT d = src[group + group_member + 3*stride];
					CVDT e = src[group + group_member + 4*stride];
					CVDT f = src[group + group_member + 5*stride];
					CVDT g = src[group + group_member + 6*stride];
					CVDT h = src[group + group_member + 7*stride];

					CVDT new_a, new_b, new_c, new_d, new_e, new_f, new_g, new_h, twid_f, twid_h;
					cvaddsub (new_a, new_e, a, e);				// a + e, a - e
					cvaddsub (new_b, new_f, b, f);				// b + f, b - f
					cvaddsub (new_c, new_g, c, g);				// c + g, c - g
					cvaddsub (new_d, new_h, d, h);				// d + h, d - h

					cvaddsub (a, c, new_a, new_c);				// a + c, a - c
					cvaddsub (b, d, new_b, new_d);				// b + d, b - d
					cviaddsub (e, g, new_e, new_g);				// e + ig, e - ig
					cviaddsub (f, h, new_f, new_h);				// f + ih, f - ih

					cvaddsub (new_a, new_b, a, b);				// a + b, a - b
					cviaddsub (new_c, new_d, c, d);				// c + id, c - id
					twid_f.real = subpd (f.real, f.imag);			// twiddled f / .707
					twid_f.imag = addpd (f.real, f.imag);
					twid_h.real = addpd (h.real, h.imag);			// -twiddled h / .707
					twid_h.imag = subpd (h.imag, h.real);
					cvaddsubfm (new_e, new_f, e, _707, twid_f);		// e + (sqrthalf+sqrthalf*i)f, e - (sqrthalf+sqrthalf*i)f
					cvsubaddfm (new_g, new_h, g, _707, twid_h);		// g + (-sqrthalf+sqrthalf*i)h, g - (-sqrthalf+sqrthalf*i)h

					dest[group + group_member] = new_a;			// Do not bit reverse outputs
					dest[group + group_member + 4*stride] = new_b;
					dest[group + group_member + 2*stride] = new_c;
					dest[group + group_member + 6*stride] = new_d;
					dest[group + group_member + 1*stride] = new_e;
					dest[group + group_member + 5*stride] = new_f;
					dest[group + group_member + 3*stride] = new_g;
					dest[group + group_member + 7*stride] = new_h;
				}
			    }
			    twiddle45_stride *= 8;
			}
		    }
		}

// If pass 1 then there is no pointmul and inverse FFT to do.  If only FFTing the input(s) also skip pointmul and inverse FFT.

		if (pd->pass == 1 || (pd->options & FORWARD_FFT_ONLY)) continue;

// Pointwise multiply

		if (pd->pass == 2) {
		    for (unsigned int j = 0; j < pd->pass_size; j++) {
			CVDT x = pd->invec1[blke * pd->pass_size + j];
			CVDT y = pd->invec2[blke * pd->pass_size + j];
			cvmul (pd->outvec[blke * pd->pass_size + j], x, y);
			if (pd->powers_of_two == 0) cvrmul (pd->outvec[blke * pd->pass_size + j], pd->inv_fft_size, pd->outvec[blke * pd->pass_size + j]);
		    }
		}

// Inverse FFT
		
		// Pass 2 reads/writes directly from/to output vector.
		// Pass 3 reads from output vector into a scratch area to reduce large strides.  At end, result written back to output vector.
		if (pd->pass == 2) {
			src = dest = pd->outvec + blke * pd->pass_size;
			instride1 = outstride1 = 1;
		} else {
			src = pd->outvec + blki;
			dest = scratch;
			instride1 = pd->pass2_size;
			outstride1 = 1;
		}

		// Inverse FFT starts with stride = 1 and moves to progressive smaller group sizes with larger strides.
		stride = 1;

		// Radix-8 FFT (if needed it is the last radix block done in pass 2)
		if (pd->pass == 2 && pd->powers_of_two & 1) {	// If odd number of powers of two do a radix-8 step
			twiddle45_stride /= 8;			// Next twiddle stride amount
			for (unsigned int group = 0; group < pd->pass_size; group += stride * 8) {
				for (unsigned int group_member = 0; group_member < stride; group_member++) {
					CVDT a = src[group + group_member];
					CVDT b = src[group + group_member + stride];
					CVDT c = src[group + group_member + 2*stride];
					CVDT d = src[group + group_member + 3*stride];
					CVDT e = src[group + group_member + 4*stride];
					CVDT f = src[group + group_member + 5*stride];
					CVDT g = src[group + group_member + 6*stride];
					CVDT h = src[group + group_member + 7*stride];

					CVDT new_a, new_b, new_c, new_d, new_e, new_f, new_g, new_h, twid_f, twid_h;

					// Do the distance = 4 round, mul by inv_fft_size as efficiently as possible
					cvrmul (a, pd->inv_fft_size, a);
					cvaddsubfm (new_a, new_e, a, pd->inv_fft_size, e);	// a + e, a - e
					cvaddsub (new_b, new_f, b, f);				// b + f, b - f
					cvaddsub (new_c, new_g, c, g);				// c + g, c - g
					cvaddsub (new_d, new_h, d, h);				// d + h, d - h

					// Do the distance = 2 round, mul by inv_fft_size as efficiently as possible
					cvaddsubfm  (a, c, new_a, pd->inv_fft_size, new_c);	// a + c, a - c
					cvisubaddfm (e, g, new_e, pd->inv_fft_size, new_g);	// e - ig, e + ig
					cvaddsub (b, d, new_b, new_d);				// b + d, b - d
					cvisubadd (f, h, new_f, new_h);				// f - ih, f + ih

					// Do the distance = 1 round, mul by inv_fft_size as efficiently as possible
					cvaddsubfm  (new_a, new_b, a, pd->inv_fft_size, b);	// a + b, a - b
					cvisubaddfm (new_c, new_d, c, pd->inv_fft_size, d);	// c - id, c + id
					twid_f.real = addpd (f.real, f.imag);			// twiddled f / .707
					twid_f.imag = subpd (f.imag, f.real);
					twid_h.real = subpd (h.real, h.imag);			// -twiddled h / .707
					twid_h.imag = addpd (h.real, h.imag);
					cvaddsubfm (new_e, new_f, e, pd->inv_fft_size_707, twid_f); // e + (sqrthalf-sqrthalf*i)f, e - (sqrthalf-sqrthalf*i)f
					cvsubaddfm (new_g, new_h, g, pd->inv_fft_size_707, twid_h); // g + (-sqrthalf-sqrthalf*i)h, g - (-sqrthalf-sqrthalf*i)h

					dest[group + group_member] = new_a;			// Do not bit reverse outputs
					dest[group + group_member + 4*stride] = new_b;
					dest[group + group_member + 2*stride] = new_c;
					dest[group + group_member + 6*stride] = new_d;
					dest[group + group_member + 1*stride] = new_e;
					dest[group + group_member + 5*stride] = new_f;
					dest[group + group_member + 3*stride] = new_g;
					dest[group + group_member + 7*stride] = new_h;
				}
			}
			stride *= 8;			// Next stride amount
		}

		// Radix-4 FFT, one fewer complex multiply than radix-2, does more work while data is in registers
		for ( ; (size = pd->pass_size / stride) % 4 == 0; stride *= 4, src = dest, instride1 = 1) {
			twiddle45_stride /= 4;
			if (pd->pass == 3 && stride * 4 == pd->pass_size && !pd->write_direct) dest = pd->outvec + blki, outstride1 = pd->pass2_size;
			for (unsigned int group = 0; group < pd->pass_size; group += stride * 4) {
				for (unsigned int group_member = 0; group_member < stride; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle45_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];
					CVDT d = src[instride1*(group + group_member + 3*stride)];

					// Special case first group member, no twiddles necessary
					CVDT	new_a, new_b, new_c, new_d;
					if (twiddle_idx == 0) {
						if (pd->pass != 2 || stride != 1) {	// Radix-8 has applied the inv_fft_size multiplier
							cvaddsub (new_a, new_c, a, c);	// a + c, a - c
							cvaddsub (new_b, new_d, b, d);	// b + d, b - d

							cvaddsub (a, b, new_a, new_b);	// a + b, a - b
							cvisubadd (c, d, new_c, new_d);	// c - id, c + id
						} else {				// Radix-8 has not applied the inv_fft_size multiplier
							cvrmul (a, pd->inv_fft_size, a);
							cvaddsubfm (new_a, new_c, a, pd->inv_fft_size, c);	// a + c, a - c
							cvaddsub   (new_b, new_d, b, d);			// b + d, b - d

							cvaddsubfm  (a, b, new_a, pd->inv_fft_size, new_b);	// a + b, a - b
							cvisubaddfm (c, d, new_c, pd->inv_fft_size, new_d);	// c - id, c + id
						}
					}
					// Apply the CONJUGATE of the twiddle
					else {
						CVDT	twiddled_b, twiddled_c, twiddled_d;
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						twidconjmuldelay (twiddled_b, b, twiddle1);
						twidconjmuldelay (twiddled_c, c, twiddle2);
						twidmuldelay     (twiddled_d, d, twiddle1);
						cvaddsubfm (new_a, new_c, a, twiddle2.sin, twiddled_c);		// a + c, a - c
						cvaddsub (new_b, new_d, twiddled_b, twiddled_d);		// b + d, b - d

						cvaddsubfm (a, b, new_a, twiddle1.sin, new_b);			// a + b, a - b
						cvisubaddfm (c, d, new_c, twiddle1.sin, new_d);			// c - id, c + id
					}

					// Save results
					dest[outstride1*(group + group_member)]            = a;		// Do not bit reverse outputs
					dest[outstride1*(group + group_member + 2*stride)] = b;
					dest[outstride1*(group + group_member + 1*stride)] = c;
					dest[outstride1*(group + group_member + 3*stride)] = d;
				}
			}
		}

		// Radix-5 FFT
		// R1= r1     +(r2+r5)     +(r3+r4)
		// R2= r1 +.309(r2+r5) -.809(r3+r4)    +.951(i2-i5) +.588(i3-i4)
		// R5= r1 +.309(r2+r5) -.809(r3+r4)    -.951(i2-i5) -.588(i3-i4)
		// R3= r1 -.809(r2+r5) +.309(r3+r4)    +.588(i2-i5) -.951(i3-i4)
		// R4= r1 -.809(r2+r5) +.309(r3+r4)    -.588(i2-i5) +.951(i3-i4)
		// I1= i1     +(i2+i5)     +(i3+i4)
		// I2= i1 +.309(i2+i5) -.809(i3+i4)    -.951(r2-r5) -.588(r3-r4)
		// I5= i1 +.309(i2+i5) -.809(i3+i4)    +.951(r2-r5) +.588(r3-r4)
		// I3= i1 -.809(i2+i5) +.309(i3+i4)    -.588(r2-r5) +.951(r3-r4)
		// I4= i1 -.809(i2+i5) +.309(i3+i4)    +.588(r2-r5) -.951(r3-r4)
		for ( ; (size = pd->pass_size / stride) % 5 == 0; stride *= 5, src = dest, instride1 = 1) {
			twiddle45_stride /= 5;
			if (pd->pass == 3 && stride * 5 == pd->pass_size && !pd->write_direct) dest = pd->outvec + blki, outstride1 = pd->pass2_size;
			VDT _309 = broadcastsd (0.30901699437494742410229341718282);
			VDT _809 = broadcastsd (0.80901699437494742410229341718282);
			VDT _951 = broadcastsd (0.95105651629515357211643933337938);
			VDT _588_951 = broadcastsd (0.61803398874989484820458683436564);
			for (unsigned int group = 0; group < pd->pass_size; group += stride * 5) {
				for (unsigned int group_member = 0; group_member < stride; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle45_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];
					CVDT d = src[instride1*(group + group_member + 3*stride)];
					CVDT e = src[instride1*(group + group_member + 4*stride)];

					// Special case first group member, no twiddles necessary
					CVDT	b_plus_e, b_minus_e, c_plus_d, c_minus_d;
					if (twiddle_idx == 0) {
						cvaddsub (b_plus_e, b_minus_e, b, e);		// b + e, b - e
						cvaddsub (c_plus_d, c_minus_d, c, d);		// c + d, c - d
					}
					// Apply the CONJUGATE of the twiddle
					else {
						CVDT twiddled_b, twiddled_c, twiddled_d, twiddled_e;
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles2[4*twiddle_idx]);
						TVDT twiddle2; twidload (twiddle2, &pmdata->twiddles2[4*twiddle_idx+2]);
						twidconjmul (twiddled_b, b, twiddle1);
						twidconjmul (twiddled_c, c, twiddle2);
						twidmuldelay (twiddled_d, d, twiddle2);
						twidmuldelay (twiddled_e, e, twiddle1);
						cvaddsubfm (b_plus_e, b_minus_e, twiddled_b, twiddle1.sin, twiddled_e);	// b + e, b - e
						cvaddsubfm (c_plus_d, c_minus_d, twiddled_c, twiddle2.sin, twiddled_d);	// c + d, c - d
					}

					CVDT tmp25a, tmp25b, tmp34a, tmp34b;
					cvaddfm (tmp25a, a, _309, b_plus_e); cvsubfm (tmp25a, tmp25a, _809, c_plus_d);	// tmp25a = a +.309*(b+e) -.809*(c+d)
					cvsubfm (tmp34a, a, _809, b_plus_e); cvaddfm (tmp34a, tmp34a, _309, c_plus_d);	// tmp34a = a -.809*(b+e) +.309*(c+d)
					cvaddfm (tmp25b, b_minus_e, _588_951, c_minus_d);				// tmp25b = delayed .951*(b-e) + 0.588*(c-d)
					cvsubfm (tmp34b, c_minus_d, _588_951, b_minus_e);				// tmp34b = delayed -.588*(b-e) + 0.951*(c-d)
					cvisubaddfm (b, e, tmp25a, _951, tmp25b);		// b = tmp25a - i*.951*tmp25b, e = tmp25a + i*.951*tmp25b
					cviaddsubfm (c, d, tmp34a, _951, tmp34b);		// c = tmp34a + i*.951*tmp34b, d = tmp34a - i*.951*tmp34b
					cvadd (a, a, b_plus_e);					// a + b + c + d + e
					cvadd (a, a, c_plus_d);

					// Save results
					dest[outstride1*(group + group_member)]            = a;
					dest[outstride1*(group + group_member + stride)]   = b;
					dest[outstride1*(group + group_member + 2*stride)] = c;
					dest[outstride1*(group + group_member + 3*stride)] = d;
					dest[outstride1*(group + group_member + 4*stride)] = e;
				}
			}
		}

		// Radix-3 FFT
		// A 3-complex inverse FFT is:
		// Res1: (R1+R2+R3) + (I1+I2+I3)i
		// Res2:  (R1-.5R2-.5R3+.866I2-.866I3) + (I1-.5I2-.5I3-.866R2+.866R3)i
		// Res3:  (R1-.5R2-.5R3-.866I2+.866I3) + (I1-.5I2-.5I3+.866R2-.866R3)i
		for ( ; (size = pd->pass_size / stride) % 3 == 0; stride *= 3, src = dest, instride1 = 1) {
			twiddle3_stride /= 3;
			if (pd->pass == 3 && stride * 3 == pd->pass_size && !pd->write_direct) dest = pd->outvec + blki, outstride1 = pd->pass2_size;
			VDT half = broadcastsd (0.5);
			VDT _866 = broadcastsd (0.86602540378443864676372317075294);
			for (unsigned int group = 0; group < pd->pass_size; group += stride * 3) {
				for (unsigned int group_member = 0; group_member < stride; group_member++) {
					uint64_t twiddle_idx = ((group_member * pd->pass_stride) + blki) * twiddle3_stride;
					CVDT a = src[instride1*(group + group_member)];
					CVDT b = src[instride1*(group + group_member + stride)];
					CVDT c = src[instride1*(group + group_member + 2*stride)];

					// Special case first group member, no twiddles necessary
					CVDT	b_plus_c, b_minus_c;
					if (twiddle_idx == 0) {
						cvadd (b_plus_c, b, c);			// b + c
						cvsub (b_minus_c, b, c);		// b - c
					}
					// Apply the CONJUGATE of the twiddle
					else {
						CVDT twiddled_b, twiddled_c;
						TVDT twiddle1; twidload (twiddle1, &pmdata->twiddles1[2*twiddle_idx]);
						twidconjmul  (twiddled_b, b, twiddle1);
						twidmuldelay (twiddled_c, c, twiddle1);
						cvaddsubfm (b_plus_c, b_minus_c, twiddled_b, twiddle1.sin, twiddled_c);	// b + c, b - c
					}

					CVDT tmp23; cvsubfm (tmp23, a, half, b_plus_c);		// tmp23 = a - 0.5 * (b + c)
					cvisubaddfm (b, c, tmp23, _866, b_minus_c);		// b = tmp23 - i * .866 * (b - c), c = tmp23 + i * .866 * (b - c)
					cvadd (a, a, b_plus_c);					// a + b + c

					dest[outstride1*(group + group_member)] = a;
					dest[outstride1*(group + group_member + stride)] = b;
					dest[outstride1*(group + group_member + 2*stride)] = c;
				}
			}
		}

		// Write directly to the eventual destination whenever possible (reduces memory bandwidth requirements in some cases).
		if (pd->write_direct) {
			// Perform a strided write from the scratch area in two-pass FFTs
			if (pd->pass == 3) write_line_slice_strided (pmdata, pd->rwlsd3, src, 0, pd->rwlsd3->size, blki, pd->pass2_size);
			// Perform an unstrided write from outvec for one-pass FFTs.  This probably provides no benefit.
			else if (pd->pass == 2 && pd->pass1_size == 1) write_line_slice_strided (pmdata, pd->rwlsd3, src, 0, pd->rwlsd3->size, 0, 1);
		}
	}
}

// Perform forward FFT, pointmul, inverse FFT in as few passes over memory as possible.

void fft_line (
	pmhandle *pmdata,			// Handle for polymult library
	fft_line_pass_data *pd)			// Structure containing all needed info to execute the FFT poly multiplication
{
	unsigned int threes_in_fft_size;	// Radix-3's contribution to fft_size

	// Generate twiddles (or use cached twiddles)
	generate_sincos (pmdata, pd->fft_size);

	// Init some needed items
	for (pd->powers_of_two = 0; (pd->fft_size & (1ULL << pd->powers_of_two)) == 0; pd->powers_of_two++);	// Count powers of two to see if radix-8 step needed
	for (threes_in_fft_size = 1; pd->fft_size % (threes_in_fft_size * 3) == 0; threes_in_fft_size *= 3);	// Count powers of three for twiddle stride calcs
	pd->inv_fft_size = broadcastsd (1.0 / (double) pd->fft_size);
	pd->inv_fft_size_707 = broadcastsd (0.70710678118654752440084436210485 / (double) pd->fft_size);

	// When the data set gets large it overflows the L2 cache.  This causes every radix-step to read & write data from main memory.  To combat
	// this we use two passes to complete the FFT, doing as much work as possible while FFT data is in the L2 cache.
	pick_pass_sizes (pmdata, pd->fft_size, &pd->pass1_size, &pd->pass2_size);

	// Do two-pass forward FFT merged with a pointmul and a two-pass inverse FFT.
	// Pass 1 is first half of the forward FFT.  Pass 2 is second half of the forward FFT, pointwise multiply, and half of the inverse FFT.
	// Pass 3 is the remaining half of the inverse FFT.
	for (pd->pass = 1; pd->pass <= 3; pd->pass++) {

		// Skip pass 1 and 3 for small FFTs (perform a one-pass FFT)
		if (pd->pass != 2 && pd->pass1_size == 1) continue;
		// Also skip pass 1 if neither input needs a forward FFT
		if (pd->pass == 1 && !(pd->options & FORWARD_FFT_INVEC1) && !(pd->options & FORWARD_FFT_INVEC2)) continue;

		// Figure out the starting twiddle strides for this pass.
		// NOTE: Assumes all radix-3 work is done in the first pass for two-pass FFTs.
		if (pd->pass == 1) {
			pd->starting_twiddle3_stride = 1;
			pd->starting_twiddle45_stride = 1;
		} else if (pd->pass == 2) {
			// If no forward FFT, start with the maximum strides to begin the inverse FFT
			if (!(pd->options & FORWARD_FFT_INVEC1) && !(pd->options & FORWARD_FFT_INVEC2)) {
				pd->starting_twiddle3_stride = threes_in_fft_size;
				pd->starting_twiddle45_stride = pd->fft_size / threes_in_fft_size;
			}
			// One pass FFTs start at 1
			else if (pd->pass1_size == 1) {
				pd->starting_twiddle3_stride = 1;
				pd->starting_twiddle45_stride = 1;
			}
			// Two pass FFTs won't perform any radix-3s in pass 2
			else {
				ASSERTG (pd->starting_twiddle3_stride = 9999);
				pd->starting_twiddle45_stride = pd->pass1_size / threes_in_fft_size;
			}
		} else {
			pd->starting_twiddle3_stride = threes_in_fft_size;
			pd->starting_twiddle45_stride = pd->pass1_size / threes_in_fft_size;
		}

		// Init some pass-specific variables
		if (pd->pass != 2) {
			pd->pass_size = pd->pass1_size;
			pd->pass_stride = pd->pass2_size;
			pd->num_exterior_blks = 1;
			pd->num_interior_blks = pd->pass2_size;
		} else {
			pd->pass_size = pd->pass2_size;
			pd->pass_stride = 1;
			pd->num_exterior_blks = pd->pass1_size;
			pd->num_interior_blks = 1;
		}

		// Launch helpers to do one pass of fft_line.  This is tricky.  There are two cases.  The most common case is polymult_line is multi-threaded
		// and fft_line_pass is single-threaded.  The less common case is polymult_line is single-threaded and fft_line_pass is multi-threaded.
		// If polymult_line is multi-threaded we cannot set pmdata->helper_opcode as all helper threads may not have dispatched based on the opcode,
		// nor can we have each thread setting pmdata->internal_callback_data.  To handle this we call fft_line_pass directly when fft_line_pass is
		// single-threaded.  When fft_line_pass is multi-threaded we use all the polymult_launch machinery, but polymult_launch must not
		// atomic_set helper_counter as polymult_line is using that variable.
		if (pmdata->mt_polymult_line) {
			pd->multithreading = FALSE;
			fft_line_pass (0, pmdata, pd);
		} else {
			atomic_set (pd->blknum, 0);
			pd->multithreading = TRUE;
			pmdata->internal_callback = &fft_line_pass;
			pmdata->internal_callback_data = pd;
			polymult_launch_helpers (pmdata);
		}
	}
}

// outvec += addin vector shifted the proper amount to create a monic polymult result
__inline void monic_line_add_hi (CVDT *fft1, CVDT *addinvec, uint64_t addinvec_size, CVDT *outvec, uint64_t outvec_size, uint64_t circular_size, bool leading_one, bool trailing_one)
{
	// Add (or multiply and add) words starting from the highest word
	int64_t outvec_index = (outvec_size-1) % circular_size;
	for (addinvec += addinvec_size-1; addinvec_size; addinvec--, addinvec_size--, outvec_index--) {
		if (outvec_index < 0) outvec_index = circular_size - 1;
		if (leading_one) { leading_one = FALSE; continue; }
		if (trailing_one && addinvec_size == 1) continue;
		if (fft1 == NULL) {
			cvadd (outvec[outvec_index], outvec[outvec_index], addinvec[0]);
		} else {
			CVDT	tmp;
			cvmul (tmp, *fft1, addinvec[0]);
			cvadd (outvec[outvec_index], outvec[outvec_index], tmp);
		}
	}
}

// outvec += addin vector shifted to the low part of outvec
__inline void monic_line_add_lo (CVDT *fft1, CVDT *addinvec, uint64_t addinvec_size, CVDT *outvec, uint64_t outvec_size, uint64_t circular_size, bool leading_one, bool trailing_one)
{
	// Add (or multiply and add) words starting from the lowest word
	for (uint64_t outvec_index = 0; addinvec_size; addinvec++, addinvec_size--, outvec_index++) {
		if (outvec_index == circular_size) outvec_index = 0;
		if (trailing_one) { trailing_one = FALSE; continue; }
		if (leading_one && addinvec_size == 1) continue;
		if (fft1 == NULL) {
			cvadd (outvec[outvec_index], outvec[outvec_index], addinvec[0]);
		} else {
			CVDT	tmp;
			cvmul (tmp, *fft1, addinvec[0]);
			cvadd (outvec[outvec_index], outvec[outvec_index], tmp);
		}
	}
}

// Make adjustments to outvec for any monic input vectors
void monic_line_adjustments (
	pmhandle *pmdata,
	CVDT	*addinvec,		// Polynomial to add to the output vector
	uint64_t addinvec_size,		// Size of the polynomial to add to the output vector
	int	addinvec_options,	// Options associated with the addin vector
	bool	addinvec_monics_stripped, // TRUE if monic ones have been stripped from addin vector
	CVDT	*outvec,		// Output polynomial
	uint64_t outvec_size,		// Size of output polynomial
	uint64_t circular_size,		// Emulating output modulo (X^circular_size - 1)
//GW	     need LSWs_skipped? gw_outvec_size?
	int	line,			// gwnum "line" number 
	int	options)		// Options for the monic vector that had its ones stripped forcing the need to add in the addin_vector
{
	gwhandle *gwdata = pmdata->gwdata;
	CVDT	fft1, *fft1p;

	// Set flags that will be useful in adding in the addin vector
	bool leading_one = (addinvec_options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC));
	bool trailing_one = (leading_one && (addinvec_options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)));

	// Adjust addin vector pointer so that it looks like monic ones have not been stripped
	if (leading_one && addinvec_monics_stripped) addinvec_size++;			// Increase length to "fake" a leading one
	if (trailing_one && addinvec_monics_stripped) addinvec--, addinvec_size++;	// Change vector start and increase length to "fake" a trailing one
   
	// If Montgomery general mod or k != 1, we need to multiply the monic add-ins by FFT(1).
	if (gwdata->GENERAL_MMGW_MOD || gwdata->k != 1.0) {
		read_write_line_slice_data rwlsd;
		prep_line (pmdata->gwdata, line, &rwlsd);
		rwlsd.data = &gwdata->GW_FFT1;
		rwlsd.size = 1;
		rwlsd.vec = &fft1;
		rwlsd.options = 0;
		rwlsd.post_process_monics = FALSE;
		read_line (pmdata, &rwlsd);
		fft1p = &fft1;
	} else {
		fft1p = NULL;
	}

	// Adjust for an implied high coefficient of one -- add in the poly to the high words
	monic_line_add_hi (fft1p, addinvec, addinvec_size, outvec, outvec_size, circular_size, leading_one, trailing_one);

	// Adjust for an implied low coefficient of one -- add in the poly to the low words
	if (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP))
		monic_line_add_lo (fft1p, addinvec, addinvec_size, outvec, outvec_size, circular_size, leading_one, trailing_one);
}

/* Preprocess a poly that will be used in multiple future polymult calls.  Preprocessing can reduce memory consumption or reduce CPU time. */
/* Returns a massaged poly.  Caller should then free the unmassaged poly.  The massaged poly cannot be used in any gwnum calls, it can only be used */
/* in future polymult calls with poly sizes and options that match those passed to this routine. */
void polymult_line_preprocess (
	pmhandle *pmdata)		// Handle for polymult library
{
	preprocess_plan *plan = (preprocess_plan *) pmdata->plan;	// Plan for how to implement an invec1 by invec2 preprocessing
	preprocessed_poly_header *hdr = plan->hdr;	// Header of the preprocessed poly under construction
	gwnum	*gw_invec1 = pmdata->invec1;		// Input poly
	uint64_t invec1_size = pmdata->invec1_size;	// Size of the input poly
	int	options = pmdata->options;
	uint64_t fft_size = plan->fft_size;
	int	line;
	int	lines_combined;				// Set when lines 0 and 1 are merged into one preprocessed output element
	CVDT	*invec1, *scratch;
	char	*first_element;				// First element in the preprocessed poly array
	uint64_t max_element_size;

	// Allocate memory for polymult preprocessing
	scratch = NULL;
	if (plan->impl == POLYMULT_IMPL_FFT && options & POLYMULT_PRE_FFT) {
		unsigned int pass1_size, pass2_size;
		pick_pass_sizes (pmdata, fft_size, &pass1_size, &pass2_size);
		if (pass1_size > 1) scratch = (CVDT *) aligned_malloc (pass1_size * sizeof (CVDT), 64);
	}

	// For non-FFTed RLPs, we only save the upper half of the expanded RLP.  Trick read_line into not expanding the RLP.
	if ((options & POLYMULT_INVEC1_RLP) && !(options & POLYMULT_PRE_FFT)) options &= ~POLYMULT_INVEC1_RLP;

	// When compressing we need to track the size of the largest compressed vector (for resizing the array smaller)
	max_element_size = 0;

	// Create pointer to first output element.  Set flag if invec1 lines 0 and 1 are merged into a single output element.
	// Lines are merged to save memory by delaying the splitting of the two reals.
	first_element = (char *) round_up_to_multiple_of ((intptr_t) hdr + sizeof (preprocessed_poly_header), 64);

	// Grab a "line" of complex values from each gwnum coefficient.  Work and copy that data set.  Then move onto remaining lines in the gwnums.
	read_write_line_slice_data rwlsd;
	rwlsd.data = gw_invec1;
	rwlsd.size = invec1_size;
	rwlsd.options = options & INVEC1_OPTIONS;
	rwlsd.post_process_monics = plan->strip_monic_from_invec1;
	while ((line = (int) atomic_fetch_incr (pmdata->helper_counter)) < pmdata->num_lines) {

		// See if we've already combined lines zero and one
		lines_combined = plan->combine_two_lines && line > 0;

		// Set invec1 so that we read the line directly into the output buffer
		invec1 = (CVDT *) (first_element + (line - lines_combined) * hdr->element_size);

		// Read a line.  If we're saving memory by delaying splitting the two reals, then combine the first two lines into one.
		if (line <= 1 && plan->combine_two_lines) {
			if (line == 1) continue;
			CVDT *temp = (CVDT *) aligned_malloc ((size_t) plan->adjusted_invec1_size * sizeof (CVDT), 64);
			prep_line (pmdata->gwdata, 0, &rwlsd);
			rwlsd.vec = temp;
			read_line (pmdata, &rwlsd);
			prep_line (pmdata->gwdata, 1, &rwlsd);
			rwlsd.vec = invec1;
			read_line (pmdata, &rwlsd);
			for (int j = 0; j < plan->adjusted_invec1_size; j++) {
				double	line0_real;
				union { VDT a; double b[VLEN / sizeof(double)]; } x;
				// Extract the real from line 0
				x.a = temp[j].real; line0_real = x.b[0];
				// Put line 0's real into invec1 from line 1
				x.a = invec1[j].imag; x.b[0] = line0_real; invec1[j].imag = x.a;
			}
			aligned_free (temp);
		} else {
			prep_line (pmdata->gwdata, line, &rwlsd);
			rwlsd.vec = invec1;
			read_line (pmdata, &rwlsd);
		}

		// Optionally forward FFT the line
		if (options & POLYMULT_PRE_FFT) {
			fft_line_pass_data fftd;
			fftd.rwlsd1 = &rwlsd;
			fftd.rwlsd3 = NULL;
			fftd.invec1 = invec1;
			fftd.invec1_size = plan->adjusted_invec1_size;
			fftd.scratch = scratch;
			fftd.options = FORWARD_FFT_INVEC1 | FORWARD_FFT_ONLY;
			fftd.fft_size = fft_size;
			fftd.write_direct = FALSE;
			fft_line (pmdata, &fftd);
		}

		// If compressing each line, compress the vector of doubles.  Track the maximum compressed vector size.
		if (options & POLYMULT_PRE_COMPRESS) {
			uint64_t compressed_element_size = compress_line ((char *) invec1, hdr->element_size, hdr->num_compressed_blks);
			ASSERTG (compressed_element_size <= hdr->element_size);
			if (compressed_element_size > max_element_size) max_element_size = compressed_element_size;
		}
	}

	// Free memory
	aligned_free (scratch);

	// Find the largest max_element_size amongst all the helpers
	gwmutex_lock (&pmdata->poly_mutex);
	if (max_element_size > plan->max_element_size) plan->max_element_size = max_element_size;
	gwmutex_unlock (&pmdata->poly_mutex);
}

// Multiply polynomials by selecting from various polymult algorithms
void polymult_line (
	pmhandle *pmdata)		// Handle for polymult library
{
	polymult_plan *plan = (polymult_plan *) pmdata->plan;		// Plan for how to implement each multiplication
	uint64_t true_invec1_size, true_invec2_size, true_outvec_size;
	uint64_t invec1_size, invec2_size, outvec_size, fft_size;
	uint32_t alloc_scratch_size;
	int	fft_options, line;
	bool	invec1_pre_ffted, invec2_pre_ffted;
	CVDT	*invec1_buffer, *invec2_buffer, *outvec_buffer, *tmpvec_buffer, *scratch_buffer;
	CVDT	*invec1;
	fft_line_pass_data fftd;
	read_write_line_slice_data rwlsd1, rwlsd2[4], rwlsd3[4];

	ASSERTG (pmdata->num_other_polys <= 4);			// It would not be hard to eliminate this restriction, just alloc the rwlsd structs

	// Grab polymult arguments out of the pmdata structure.  They were copied there so all threads have access to them.
	gwnum	*gw_invec1 = pmdata->invec1;			// First input poly
	uint64_t gw_invec1_size = pmdata->invec1_size;		// Size of the first input polynomial

	// Check for pre-processed input poly
	invec1_pre_ffted = is_preprocessed_poly (gw_invec1) && is_preffted_poly (gw_invec1);

	// Calculate the scratch area size.  This is the one buffer size that cannot be calculated during the planning process.
	// If fft_line is multi-threaded we need a scratch buffer for each helper that fft_line might spawn.
	alloc_scratch_size = 0;
	for (int i = 0; i < pmdata->num_other_polys; i++) {
		uint32_t pass1_size, pass2_size;
		if (plan->planpart[i].impl != POLYMULT_IMPL_FFT) continue;
		pick_pass_sizes (pmdata, plan->planpart[i].fft_size, &pass1_size, &pass2_size);
		if (pass1_size > 1 && pass1_size > alloc_scratch_size) alloc_scratch_size = pass1_size;
	}
	if (!pmdata->mt_polymult_line) alloc_scratch_size *= pmdata->num_threads;

	// Allocate memory for polymult.  For the output vector allocate two extra in case there are any monic RLP inputs.
	invec1_buffer = (CVDT *) (plan->alloc_invec1_size ? aligned_malloc ((size_t) plan->alloc_invec1_size * sizeof (CVDT), 64) : NULL);
	invec2_buffer = (CVDT *) (plan->alloc_invec2_size ? aligned_malloc ((size_t) plan->alloc_invec2_size * sizeof (CVDT), 64) : NULL);
	tmpvec_buffer = (CVDT *) (plan->alloc_tmpvec_size ? aligned_malloc ((size_t) plan->alloc_tmpvec_size * sizeof (CVDT), 64) : NULL);
	outvec_buffer = (CVDT *) aligned_malloc ((size_t) plan->alloc_outvec_size * sizeof (CVDT), 64);
	scratch_buffer = (CVDT *) (alloc_scratch_size ? aligned_malloc ((size_t) alloc_scratch_size * sizeof (CVDT), 64) : NULL);

//GW:   alternative if poly FFT size is large, reread invec1/2 in order to post process addin to save memory! Would this help P-1??? NO.
//GW:		code would be similar to fma processing

	// Fill in rwlsd1 as much as we can
	invec1 = (invec1_buffer == NULL ? tmpvec_buffer : invec1_buffer);
	rwlsd1.data = gw_invec1;
	rwlsd1.size = gw_invec1_size;
	rwlsd1.vec = invec1;
	rwlsd1.options = pmdata->options & INVEC1_OPTIONS;

	// Fill in rwlsd2 and rwlsd3 structures as much as we can for each other_poly
	for (int i = 0; i < pmdata->num_other_polys; i++) {
		int	options = pmdata->options | pmdata->other_polys[i].options;
		struct planpart *planpart = &plan->planpart[i];

		// Reload saved fftsize, post_monics, adjusted_sizes and anything else we need from the plan computed in polymult_several
		bool	strip_monic_from_invec1;	// True if ones are stripped from monic invec1 requiring invec2 to be added in during post processing
		bool	post_monic_addin_invec2;	// True if ones are stripped from monic invec1 requiring invec2 to be added in during post processing
		bool	strip_monic_from_invec2;	// True if ones are stripped from monic invec2 requiring invec1 to be added in during post processing
		bool	post_monic_addin_invec1;	// True if ones are stripped from monic invec2 requiring invec1 to be added in during post processing
		strip_monic_from_invec1 = planpart->strip_monic_from_invec1;
		strip_monic_from_invec2 = planpart->strip_monic_from_invec2;
		post_monic_addin_invec1 = planpart->post_monic_addin_invec1;
		post_monic_addin_invec2 = planpart->post_monic_addin_invec2;

		// Finish setup of rwlsd1
		rwlsd1.post_process_monics = strip_monic_from_invec1;		//GW: Technically this can be set outside of this loop

		// Read invec2.  For FFT implementation we can usually read invec2 into a buffer shared with outvec.
		CVDT *outvec = outvec_buffer + planpart->adjusted_shift;
		CVDT *invec2 = (planpart->impl == POLYMULT_IMPL_FFT && !post_monic_addin_invec2) ? outvec : invec2_buffer;
		rwlsd2[i].data = pmdata->other_polys[i].invec2;		// Second input poly
		rwlsd2[i].size = pmdata->other_polys[i].invec2_size;	// Size of the second input polynomial
		rwlsd2[i].vec = invec2;
		rwlsd2[i].options = options & INVEC2_OPTIONS;
		rwlsd2[i].post_process_monics = strip_monic_from_invec2;

		// Copy outvec to the output gwnums.  Also apply optional FMA operations.
		rwlsd3[i].data = pmdata->other_polys[i].outvec;		// Output poly of size invec1_size + invec2_size - 1
		rwlsd3[i].size = pmdata->other_polys[i].outvec_size;	// Size of the output polynomial.  Should be invec1_size + invec2_size (less one if not monic).
		rwlsd3[i].vec = outvec_buffer;
		rwlsd3[i].fma_data = pmdata->other_polys[i].fmavec;		// Addin for FMA operations
		rwlsd3[i].LSWs_skipped = planpart->LSWs_skipped;		// Entries to ignore in vec
		rwlsd3[i].streamed_stores = planpart->streamed_stores;		// Optional streaming stores
		rwlsd3[i].options = options;
	}

	// Grab a "line of complex values" from each gwnum coefficient.  Work that data set.  Then move onto remaining lines in the gwnums.
	while ((line = (int) atomic_fetch_incr (pmdata->helper_counter)) < pmdata->num_lines) {
	    CVDT *invec2, *outvec, *true_outvec, *tmpvec, *scratch;
	    tmpvec = tmpvec_buffer;
	    scratch = scratch_buffer;

	    // Prepare for read_line and write_line calls
	    prep_line (pmdata->gwdata, line, &rwlsd1);

	    // Loop over all the other polys to multiply with the first poly
	    uint64_t prev_fft_size = 0;
	    for (int i = 0; i < pmdata->num_other_polys; i++) {
		gwnum	*gw_invec2 = pmdata->other_polys[i].invec2;		// Second input poly
		gwnum	*gw_outvec = pmdata->other_polys[i].outvec;		// Output poly of size invec1_size + invec2_size - 1
		uint64_t gw_outvec_size = pmdata->other_polys[i].outvec_size;	// Size of the output polynomial.  Should be invec1_size + invec2_size (less one if not monic).
		int	options = pmdata->options | pmdata->other_polys[i].options;
		struct planpart *planpart = &plan->planpart[i];

		// Prepare for read_line and write_line calls
		rwlsd2[i].lsd = rwlsd1.lsd;			// Copy data initialized by prep_line
		rwlsd3[i].lsd = rwlsd1.lsd;			// Copy data initialized by prep_line

		// Reload saved fftsize, post_monics, adjusted_sizes and anything else we need from the plan computed in polymult_several
		bool	strip_monic_from_invec1;	// True if ones are stripped from monic invec1 usually requiring invec2 to be added in during post processing
		bool	post_monic_addin_invec2;	// True if ones are stripped from monic invec1 requiring invec2 to be added in during post processing
		bool	strip_monic_from_invec2;	// True if ones are stripped from monic invec2 usually requiring invec1 to be added in during post processing
		bool	post_monic_addin_invec1;	// True if ones are stripped from monic invec2 requiring invec1 to be added in during post processing
		uint64_t circular_size, LSWs_skipped;

		strip_monic_from_invec1 = planpart->strip_monic_from_invec1;
		strip_monic_from_invec2 = planpart->strip_monic_from_invec2;
		post_monic_addin_invec1 = planpart->post_monic_addin_invec1;
		post_monic_addin_invec2 = planpart->post_monic_addin_invec2;
		fft_size = planpart->fft_size;
		circular_size = planpart->circular_size;
		LSWs_skipped = planpart->LSWs_skipped;

		// When monic ones are stripped from inputs, the brute/Karatsuba/polyFFT code creates an output that is smaller than the true outvec result size.
		// The unprefixed variables refer to the monic stripped input and output sizes, the true_XXX variables refer to the full input and output sizes.
		invec1_size = planpart->adjusted_invec1_size;
		invec2_size = planpart->adjusted_invec2_size;
		outvec_size = planpart->adjusted_outvec_size;
		true_invec1_size = planpart->true_invec1_size;
		true_invec2_size = planpart->true_invec2_size;
		true_outvec_size = planpart->true_outvec_size;

		// Pad/shift the outvec pointer so that multiplication results land in the correct location of the true outvec.
		true_outvec = outvec_buffer;
		outvec = outvec_buffer + planpart->adjusted_shift;

		// Pad output vector on the right with a zero or two to reach the true_outvec_size
		for (int i = 0; i < planpart->adjusted_shift; i++) memset (&true_outvec[i], 0, sizeof (CVDT));
		// Pad output vector on the left with a zero or two to reach the true_outvec_size
		for (int i = 0; i < planpart->adjusted_pad; i++) memset (&true_outvec[outvec_size+planpart->adjusted_shift+i], 0, sizeof (CVDT));

		// Check for pre-processed other poly
		invec2_pre_ffted = is_preprocessed_poly (gw_invec2) && is_preffted_poly (gw_invec2);

		// Read invec1 on processing of first other poly.  For FFT implementation we usually read invec1 into a buffer shared
		// with tmpvec (the buffer that will hold the FFT of invec1).
		if (i == 0) read_line (pmdata, &rwlsd1);
		// Plan should not change strip_monic_from_invec1 so that we can read invec1 just once (very important since outvecs can output to invec1)
		ASSERTG (i == 0 || strip_monic_from_invec1 == plan->planpart[i-1].strip_monic_from_invec1);

		// Read invec2.  For FFT implementation we can usually read invec2 into a buffer shared with outvec.
		invec2 = (planpart->impl == POLYMULT_IMPL_FFT && !post_monic_addin_invec2) ? outvec : invec2_buffer;
		read_line (pmdata, &rwlsd2[i]);

		// Now execute a brute force implementation.  Brute_line does implement circular_size, that will not need to be emulated later.
		// Because brute_line implements circular_size, it must get true_outvec buffer.
		// Brute_line does peer into gw_outvec to see if output coefficients need to be computed -- a minor optimization.
		if (planpart->impl == POLYMULT_IMPL_BRUTE) {
			brute_line (invec1, invec1_size, invec2, invec2_size, true_outvec, circular_size, gw_outvec, gw_outvec_size, planpart->adjusted_shift, LSWs_skipped);
		}

		// Now execute a Karatsuba implementation
 		else if (planpart->impl == POLYMULT_IMPL_KARATSUBA) {
			karatsuba_line (pmdata, invec1, invec1_size, invec2, invec2_size, tmpvec, outvec);
			// Clear poly FFT size for next invec2 since we changed tmpvec
			prev_fft_size = 0;
		}

		// Now execute a poly FFT implementation
		else {
			// FFT invec1 only on first other_poly OR changed fft sizes
			if (invec1_pre_ffted || (i > 0 && fft_size == prev_fft_size)) fft_options = 0;
			else fft_options = FORWARD_FFT_INVEC1;
			// FFT invec2 unless it was done during pre-processing
			if (!invec2_pre_ffted) fft_options |= FORWARD_FFT_INVEC2;
			// If we're FFTing invec1, copy and/or zero pad as necessary
			if (fft_options & FORWARD_FFT_INVEC1) {
//GW				if (invec1 != tmpvec) memcpy (tmpvec, invec1, (size_t) invec1_size * sizeof (CVDT));
//GW				memset (tmpvec + invec1_size, 0, (size_t) (fft_size - invec1_size) * sizeof (CVDT));
			}
			// If we're FFTing invec2, copy and/or zero pad as necessary
			if (fft_options & FORWARD_FFT_INVEC2) {
//GW				if (invec2 != outvec) memcpy (outvec, invec2, (size_t) invec2_size * sizeof (CVDT));
//GW				memset (outvec + invec2_size, 0, (size_t) (fft_size - invec2_size) * sizeof (CVDT));
			}
			// Now do the necessary forward FFTs, pointwise multiply, and inverse FFT.
//GW: Have fft_line detect pre-ffted inputs (or set flag here?)
//GW: Some of these can be set outside of the loop
			fftd.rwlsd1 = &rwlsd1;
			fftd.rwlsd2 = &rwlsd2[i];
			fftd.rwlsd3 = &rwlsd3[i];
			fftd.invec1 = tmpvec;
			fftd.invec1_size = invec1_size;
			fftd.invec2 = outvec;
			fftd.invec2_size = invec2_size;
			fftd.outvec = outvec;
			fftd.scratch = scratch;
			fftd.options = fft_options;
			fftd.fft_size = fft_size;
			fftd.write_direct = (fft_size < pmdata->strided_writes_end && !planpart->emulate_circular &&
					     !post_monic_addin_invec1 && !post_monic_addin_invec2);
			fft_line (pmdata, &fftd);
			// Remember poly FFT size for next invec2
			prev_fft_size = fft_size;
		}

		// Emulate circular_size when necessary.  We only do this for coefficients that might actual be returned to the caller.
		if (planpart->emulate_circular) {
			uint64_t i, adjusted_i;		// i is index into true_outvec, adjusted_i is i % circular_size
			for (i = circular_size + LSWs_skipped, adjusted_i = LSWs_skipped; i < true_outvec_size; ) {
				if (adjusted_i >= LSWs_skipped + gw_outvec_size) {
					i = divide_rounding_up (i, circular_size) * circular_size + LSWs_skipped;
					adjusted_i = LSWs_skipped;
				} else {
					cvadd (true_outvec[adjusted_i], true_outvec[adjusted_i], true_outvec[i]);
					i++, adjusted_i++;
				}
			}
		}

		// Handle adjustments needed due to monic inputs
//GW: circular monic adjust? mulhi/mid/lo can skip chunks of addin?
		if (post_monic_addin_invec1)
			monic_line_adjustments (pmdata, invec1, invec1_size, options & POLYMULT_INVEC1_MONIC_RLP, strip_monic_from_invec1,
						true_outvec, true_outvec_size, circular_size, line, options & POLYMULT_INVEC2_MONIC_RLP);
		if (post_monic_addin_invec2)
			monic_line_adjustments (pmdata, invec2, invec2_size, options & POLYMULT_INVEC2_MONIC_RLP, strip_monic_from_invec2,
						true_outvec, true_outvec_size, circular_size, line, options & POLYMULT_INVEC1_MONIC_RLP);

//GWDo we emulate circular before or after monic_line_adjustments???  Do we avoid adjusting for values that are not returned??
//GWwe need to know where the top is when we're not optimizing circular			
//GW	true_outvec_size = 7	circular_size = 7			MSWs_skipped + outvec_size + LSWs_skipped = 7
//GWwe need to know where the top is when we've decided to optimizing circular
//GW	true_outvec_size = 7?   circular_size = 5			MSWs_skipped + outvec_size + LSWs_skipped = 7
//GWwe need to know where the top is when we've decided to ideal circular
//GW	true_outvec_size = 5   circular_size = 5			MSWs_skipped + outvec_size + LSWs_skipped = 5

		// Copy outvec to the output gwnums.  Also apply optional FMA operations.
		if (planpart->impl != POLYMULT_IMPL_FFT || !fftd.write_direct) write_line (pmdata, &rwlsd3[i]);
	    }
	}

	// Free memory
	aligned_free (invec1_buffer);
	aligned_free (invec2_buffer);
	aligned_free (outvec_buffer);
	aligned_free (tmpvec_buffer);
	aligned_free (scratch_buffer);
}

#endif


/*--------------------------------------------------------------------------
|			Polymult helper threads
+-------------------------------------------------------------------------*/

#if !defined (SSE2) && !defined (AVX) && !defined (FMA) && !defined (AVX512)

/* Have the helper thread call the appropriate task */
void polymult_dispatch (
	int	helper_num,			// Helper number (main thread is 0, launched threads are 1+)
	pmhandle *pmdata,
	gwhandle *gwdata)			// Cloned gwdata (may not be initialized for internal polymult helpers!)
{
	// Call internal routine (read_line_slice, fft_line_pass, write_line_slice) that is nested within polymult_line and cannot overwrite helper_opcode
	if (pmdata->internal_callback != NULL) {
		(*pmdata->internal_callback) (helper_num, pmdata, pmdata->internal_callback_data);
		return;
	}
	// Call user-defined routine with a cloned gwdata so the user can do multi-threaded gwnum work
	switch (pmdata->helper_opcode) {
	case 0:
		// Call the user-defined helper routine
		(*pmdata->helper_callback) (helper_num, gwdata, pmdata->helper_callback_data);
		break;

	// Pre-FFT polymult inputs
	case HELPER_PRE_FFT:
		polymult_pre_fft (pmdata, gwdata);
		break;

	// Polymult_line optimized for several instruction sets
	case HELPER_POLYMULT_LINE:
#ifdef X86_64
		// Polymult_line optimized routines for AVX512 instructions
		if (pmdata->gwdata->cpu_flags & CPU_AVX512F) polymult_line_avx512 (pmdata); else
#endif
		// Polymult optimized routines for FMA3 instructions
		if (pmdata->gwdata->cpu_flags & CPU_FMA3) polymult_line_fma (pmdata);
		// Polymult optimized routines for AVX instructions
		else if (pmdata->gwdata->cpu_flags & CPU_AVX) polymult_line_avx (pmdata);
		// Polymult optimized routines for SSE2 instructions
		else if (pmdata->gwdata->cpu_flags & CPU_SSE2) polymult_line_sse2 (pmdata);
		// Polymult default routines using no vector instructions
#ifndef X86_64
		else polymult_line_dbl (pmdata);
#endif
		break;

	// Post-unFFT polymult outputs
	case HELPER_POST_UNFFT:
		polymult_post_unfft (pmdata, gwdata);
		break;

	// Polymult_preprocess_line optimized for several instruction sets
	case HELPER_PREPROCESS_LINE:
#ifdef X86_64
		if (pmdata->gwdata->cpu_flags & CPU_AVX512F) polymult_line_preprocess_avx512 (pmdata); else
#endif
		if (pmdata->gwdata->cpu_flags & CPU_FMA3) polymult_line_preprocess_fma (pmdata);
		else if (pmdata->gwdata->cpu_flags & CPU_AVX) polymult_line_preprocess_avx (pmdata);
		else if (pmdata->gwdata->cpu_flags & CPU_SSE2) polymult_line_preprocess_sse2 (pmdata);
#ifndef X86_64
		else polymult_line_preprocess_dbl (pmdata);
#endif
		break;
	}

}

/* Entry point for helper thread to do part of the polymult */
void polymult_thread (void *arg)
{
	pmhandle *pmdata = (pmhandle *) arg;
	gwhandle cloned_gwdata;
	bool	cloned_gwdata_initialized = FALSE;

/* Generate a unique helper thread number (so that callback routine to uniquely identify helper threads) */
/* Call gwnum's optional user provided callback routine so that the caller can set the thread's priority and affinity */

	int thread_num = (int) atomic_incr_fetch (pmdata->next_thread_num);
	if (pmdata->gwdata->thread_callback != NULL)
		(*pmdata->gwdata->thread_callback) (thread_num, 20, pmdata->gwdata->thread_callback_data);

/* Loop doing polymult or user-defined work */

	for ( ; ; ) {
	    int	num_active_helpers;

	    // Wait on the work-to-do event for more work (or termination).  There are two different ways to wait for work.
	    if (pmdata->gwdata->use_spin_wait > thread_num) atomic_spinwait (pmdata->alt_work_to_do, 1);
	    else gwevent_wait (&pmdata->work_to_do, 0);

	    // Terminate helper when polymult_done is called
	    if (pmdata->helper_opcode == HELPER_EXIT) break;

	    /* IMPORTANT:  Helpers are considered done as soon as all_work_assigned is set and num_active_helpers reaches zero.  When this happens the main */
	    /* thread is allowed to resume.  However, there may be some helpers that have not yet been given a time slice after the work_to_do event was */
	    /* signalled!  We  must catch these workers right here.  If they were to get past this section, they could read some inconsistent state that */
	    /* the main thread has changed in preparation for the next batch of work for the helper threads. */
	    if (pmdata->all_work_assigned) continue;
	    num_active_helpers = (int) atomic_incr_fetch (pmdata->num_active_helpers);
	    if (!pmdata->all_work_assigned) {		// Check for rare case of all_work_assigned set during atomic_incr_fetch

		// Dispatch unless caller requests using fewer than the maximum number of helper threads
		if (num_active_helpers < pmdata->num_threads) {
			    
		    // User-defined callbacks and some polymult helpers require a cloned gwdata
		    if (!cloned_gwdata_initialized && pmdata->helper_opcode != HELPER_PREPROCESS_LINE && pmdata->helper_opcode != HELPER_POLYMULT_LINE) {
			gwclone (&cloned_gwdata, pmdata->gwdata);		// Clone gwdata
			cloned_gwdata_initialized = TRUE;
		    }

		    // Dispatch to perform the correct work
		    polymult_dispatch (thread_num, pmdata, &cloned_gwdata);

		    // Helpers that used a cloned gwdata require gwdata stats to be merged
		    if (pmdata->helper_opcode != HELPER_PREPROCESS_LINE && pmdata->helper_opcode != HELPER_POLYMULT_LINE) {
			gwmutex_lock (&pmdata->poly_mutex);
			if (pmdata->stats_gwdata == NULL) pmdata->stats_gwdata = &cloned_gwdata;	// Nothing to add our stats to yet
			else gwclone_merge_stats (pmdata->stats_gwdata, &cloned_gwdata);		// Add our stats to pmdata->stats_gwdata
			gwmutex_unlock (&pmdata->poly_mutex);
		    }
		}

		// No more work to assign to helper threads
		pmdata->all_work_assigned = TRUE;		// Set flag so any helper threads that have not yet started don't try to do any work
		gwevent_reset (&pmdata->work_to_do);		// Reset event saying there is work for helper threads to do
		if (pmdata->gwdata->use_spin_wait >= 2) atomic_set (pmdata->alt_work_to_do, 0);
	    }

	    // When num active helpers reaches zero, main thread can wake up (either by waiting on all_helpers_done or via a spin wait)
	    // WARNING: It is possible for a straggler thread from the previous work_to_do event to lose its time slice just before the all_helpers_done signal call.
	    // This can cause spurious all_helpers_done signals for the current work_to_do event.  The main thread must be very careful resuming from the
	    // all_helpers_done wait by checking that num_active_helpers is in fact zero.
	    if (atomic_decr_fetch (pmdata->num_active_helpers) == 0) gwevent_signal (&pmdata->all_helpers_done);
	}

/* Call gwnum's optional user provided callback routine so that the caller can do any necessary cleanup */

	if (pmdata->gwdata->thread_callback != NULL)
		(*pmdata->gwdata->thread_callback) (thread_num, 21, pmdata->gwdata->thread_callback_data);

/* Free the cloned gwdata that user-defined callbacks and some internal helpers used */

	if (cloned_gwdata_initialized) gwdone (&cloned_gwdata);
}

/* This routine launches the polymult helper threads.  Has the calling thread also help, then waits for the calling thread and all helper threads to finish. */
/* The polymult library uses this routine to do multi-threading, you can too! */
void polymult_launch_helpers (
	pmhandle *pmdata)		// Handle for polymult library
{
	int	num_threads;		// Total number of threads to use to complete this task

	// Clear atomic counter for both polymult helpers and user helpers to use.  We cannot do this for read_line_slice/fft_line_pass/write_line_slice
	// as this is launched from within HELPER_POLYMULT_LINE which is using pmdata->helper_counter.
	if (pmdata->internal_callback == NULL) atomic_set (pmdata->helper_counter, 0);	// Reset counter to no-work-done-yet state

	// Compute how many threads we want to use.  Normally this is all available threads.  However, HELPER_POLYMULT_LINE and
	// read_line_slice/fft_line_pass/write_line_slice are special in that only one or the other can multi-thread.
	num_threads = pmdata->num_threads;
	if (pmdata->helper_opcode == HELPER_POLYMULT_LINE && !pmdata->mt_polymult_line && pmdata->internal_callback == NULL) num_threads = 1;

	// Launch helpers
	if (num_threads > 1) {
		// "Neuter" gwnum to single-threaded when helpers are involved
		pmdata->saved_gwdata_num_threads = gwget_num_threads (pmdata->gwdata);
		gwset_num_threads (pmdata->gwdata, 1);
		// Launch helpers for the first time
		if (pmdata->thread_ids == NULL) {
			atomic_set (pmdata->next_thread_num, 0);
			gwmutex_init (&pmdata->poly_mutex);
			gwevent_init (&pmdata->work_to_do);
			gwevent_init (&pmdata->all_helpers_done);
			atomic_set (pmdata->alt_work_to_do, 0);			// No work for helpers to do yet
			pmdata->thread_ids = (gwthread *) malloc (pmdata->max_num_threads * sizeof (gwthread));
			for (int i = 1; i < pmdata->max_num_threads; i++) gwthread_create_waitable (&pmdata->thread_ids[i], &polymult_thread, (void *) pmdata);
		}
		// Activate helpers.  They're waiting for work to do signal.
		pmdata->stats_gwdata = NULL;
		pmdata->all_work_assigned = FALSE;			// When this is set and num_active_helpers reaches 0, it is safe to signal main thread
		gwevent_signal (&pmdata->work_to_do);			// Start all helper threads
		if (pmdata->gwdata->use_spin_wait >= 2) atomic_set (pmdata->alt_work_to_do, 1);
	}

	// Have this thread help too.  Dispatch to perform the correct work using the original gwnum rather than a cloned gwnum.
	polymult_dispatch (0, pmdata, pmdata->gwdata);

	// Sync up with helper threads
	if (num_threads > 1) {
		// This thread is done, set no more work to assign to helper threads
		pmdata->all_work_assigned = TRUE;
		gwevent_reset (&pmdata->work_to_do);
		if (pmdata->gwdata->use_spin_wait >= 2) atomic_set (pmdata->alt_work_to_do, 0);
		// Wait for helpers to end
		if (pmdata->gwdata->use_spin_wait) atomic_spinwait (pmdata->num_active_helpers, 0);
		else while (atomic_get (pmdata->num_active_helpers)) {
			gwevent_reset (&pmdata->all_helpers_done);
			if (atomic_get (pmdata->num_active_helpers)) gwevent_wait (&pmdata->all_helpers_done, 0);
		}
		// Helpers require syncing up cloned gwdata stats and restoring main thread's gwnum's multi-threading capabilities
		if (pmdata->stats_gwdata != NULL) gwclone_merge_stats (pmdata->gwdata, pmdata->stats_gwdata);	// Add pmdata->stats_gwdata to main thread stats
		gwset_num_threads (pmdata->gwdata, pmdata->saved_gwdata_num_threads);
	}

	// Reset opcode to user defined work
	pmdata->helper_opcode = 0;
	pmdata->internal_callback = NULL;
}

/*--------------------------------------------------------------------------
|			Polymult main routines
+-------------------------------------------------------------------------*/

/* Preprocess a poly that will be used in multiple future polymult calls.  Preprocessing can reduce memory consumption or reduce CPU time. */
/* Returns a massaged poly.  Caller should then free the unmassaged poly.  The massaged poly cannot be used in any gwnum calls, it can only be used */
/* in future polymult calls with poly sizes and options that match those passed to this routine. */
gwarray polymult_preprocess (		// Returns a plug-in replacement for the input poly
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// Input poly
	uint64_t invec1_size,		// Size of the input polynomial
	uint64_t invec2_size,		// Size of the other polynomial that will be used in a future polymult call
	uint64_t outvec_size,		// Size of the output polynomial that will be used in a future polymult call
	int	options)		// Future polymult options preprocessing options (FFT, compress -- see polymult.h)
{
	preprocess_plan plan;		// Plan for how to implement preprocessing invec1 
	int	num_elements;
	uint64_t element_size;
	bool	must_fft;

	// Cannot preprocess an already preprocessed poly
	ASSERTG (!is_preprocessed_poly (invec1));

	// Adjust sizes due to RLPs.  Calculate sizes assuming no monic ones in data and sizes assuming monic ones are in the data.
	plan.adjusted_invec1_size = (options & POLYMULT_INVEC1_RLP) ? 2 * invec1_size - 1 : invec1_size;
	plan.adjusted_invec2_size = (options & POLYMULT_INVEC2_RLP) ? 2 * invec2_size - 1 : invec2_size;
	plan.adjusted_outvec_size = plan.adjusted_invec1_size + plan.adjusted_invec2_size - 1;
	plan.true_invec1_size = plan.adjusted_invec1_size + ((options & POLYMULT_INVEC1_MONIC) ? ((options & POLYMULT_INVEC1_RLP) ? 2 : 1) : 0);
	plan.true_invec2_size = plan.adjusted_invec2_size + ((options & POLYMULT_INVEC2_MONIC) ? ((options & POLYMULT_INVEC2_RLP) ? 2 : 1) : 0);
	plan.true_outvec_size = plan.true_invec1_size + plan.true_invec2_size - 1;

	// Sometimes we must (or it is beneficial to) handle monic input polynomials as part of invec1/invec2 as opposed to a post-processing call to
	// monic_line_adjustments.  Find those cases and adjust vector sizes.

	// If both inputs are monic, one of the inputs must have their ones stripped.  It has to be invec1 since once we FFT the poly invec1 data is gone.
	// Stripping ones from invec1 means post proceessing will add invec2.
	if ((options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC2_MONIC)) {
		plan.strip_monic_from_invec1 = TRUE;
	}
	// Next case is a circular multiply where the poly FFT can do the circular operation for free
	else if (options & POLYMULT_CIRCULAR && outvec_size == polymult_fft_size (outvec_size)) {
		plan.strip_monic_from_invec1 = FALSE;
		plan.adjusted_invec1_size = plan.true_invec1_size;
		plan.adjusted_invec2_size = plan.true_invec2_size;
		plan.adjusted_outvec_size = outvec_size;
	}
	// Next case is where including the monic ones would result in a larger FFT size
	else if (polymult_fft_size (plan.adjusted_outvec_size) < polymult_fft_size (plan.true_outvec_size)) {
		plan.strip_monic_from_invec1 = TRUE;
	}
	// Finally, include the monic ones
	else {
		plan.strip_monic_from_invec1 = FALSE;
		plan.adjusted_invec1_size = plan.true_invec1_size;
		plan.adjusted_invec2_size = plan.true_invec2_size;
		plan.adjusted_outvec_size = plan.true_outvec_size;
	}

	// Allocate memory for polymult preprocessing
	must_fft = FALSE;
//GW: The POLYMULT_FFT option is not a commandment to pre-FFT the poly.  It a request to pre-FFT if polymult would normally choose to do an FFT multiply.
//GW: Do we want a POLYMULT_FORCE_FFT option?  or have caller temporarily set BRUTE_BREAK and KARAT_BREAK?
//	must_fft = options & POLYMULT_FORCE_FFT;
	if ((!must_fft && plan.adjusted_outvec_size < pmdata->KARAT_BREAK)) {
		plan.impl = POLYMULT_IMPL_BRUTE;
		options &= ~POLYMULT_PRE_FFT;
	}
	else if (!must_fft && !(options & POLYMULT_CIRCULAR) && plan.adjusted_outvec_size < pmdata->FFT_BREAK) {
		plan.impl = POLYMULT_IMPL_KARATSUBA;
		options &= ~POLYMULT_PRE_FFT;
	}
	else {
		plan.impl = POLYMULT_IMPL_FFT;
		plan.fft_size = polymult_fft_size (plan.adjusted_outvec_size);
	}

	// If not-FFTing then strip monic ones so that read_line won't add 1+0i to invec1.  We'll add 1+0i later in read_preprocess_line.
	if (!(options & POLYMULT_PRE_FFT)) {
		plan.strip_monic_from_invec1 = TRUE;
		plan.adjusted_invec1_size = invec1_size;
	}

// Allocate space for the preprocessed poly under construction

	// Calculate complex vector size in bytes
	int complex_vector_size = complex_vector_size_in_bytes (pmdata->gwdata->cpu_flags);
	// Compute size of each line to be written
	element_size = complex_vector_size * (uint64_t) ((options & POLYMULT_PRE_FFT) ? plan.fft_size : invec1_size);

	// Allocate and init the preprocessed output
	plan.combine_two_lines = !pmdata->gwdata->NEGACYCLIC_FFT && !pmdata->gwdata->ZERO_PADDED_FFT && !(options & POLYMULT_PRE_FFT);
	num_elements = plan.combine_two_lines ? pmdata->num_lines - 1 : pmdata->num_lines;
	plan.hdr = (preprocessed_poly_header *) malloc ((size_t) (sizeof (preprocessed_poly_header) + 64 + num_elements * element_size));
	if (plan.hdr == NULL) return (NULL);
	memset (plan.hdr, 0, sizeof (preprocessed_poly_header));
	plan.hdr->element_size = element_size;
	plan.hdr->options = options;
	plan.hdr->monic_ones_included = !plan.strip_monic_from_invec1;
	plan.hdr->top_unnorms = unnorms (invec1[invec1_size-1]);
	plan.hdr->line_size = (options & POLYMULT_PRE_FFT) ? plan.fft_size : invec1_size;
	// Compress large lines (more than 32K doubles) in blocks to allow multi-threaded read.  Blocks are typically 512 doubles.
	if (pmdata->num_threads == 1 || element_size < 32768 * sizeof (double)) plan.hdr->num_compressed_blks = 1;
	else for (int i = 1; i <= 512 && element_size % (i * sizeof (double)) == 0; i *= 2) plan.hdr->num_compressed_blks = (int) (element_size / (i * sizeof (double)));

	// Prepare for polymult_preprocess in parallel
	pmdata->plan = &plan;
	pmdata->invec1 = invec1;
	pmdata->invec1_size = invec1_size;
	pmdata->num_other_polys = 0;
	pmdata->options = options;
	plan.max_element_size = 0;

	// FFT all the inputs
	pmdata->helper_opcode = HELPER_PRE_FFT;		// The helpers are doing pre-FFT polymult work
	polymult_launch_helpers (pmdata);

	// Fire up each helper thread to do the polymult_preprocess work.  Set flag so fft_line does not launch helpers to process fft_line_pass.
	pmdata->mt_polymult_line = TRUE;
	pmdata->helper_opcode = HELPER_PREPROCESS_LINE;	// The helpers are doing polymult_preprocess work
	polymult_launch_helpers (pmdata);
	
	// Move data to shrink compressed poly to its minimum possible size, then use realloc to free memory.
	if (options & POLYMULT_PRE_COMPRESS) {
		uint64_t uncompressed_blk_size = element_size / plan.hdr->num_compressed_blks;
		uint64_t compressed_blk_size = (plan.max_element_size - 1) / plan.hdr->num_compressed_blks;

//GW: This is hard to multi-thread!
		char *first_element = (char *) round_up_to_multiple_of ((intptr_t) plan.hdr + sizeof (preprocessed_poly_header), 64);
		for (int line = 0; line < num_elements; line++) {
			char *src = first_element + line * element_size;
			char *dest = first_element + line * plan.max_element_size;
			// Copy one header byte for each line
			*dest++ = *src++;
			// Copy each slice of the line
			for (int blk = 0; blk < plan.hdr->num_compressed_blks; blk++) {
				memmove (dest, src, (size_t) compressed_blk_size);
				dest += compressed_blk_size;
				src += uncompressed_blk_size;
			}
		}
		plan.hdr = (preprocessed_poly_header *) realloc (plan.hdr, (size_t) (sizeof (preprocessed_poly_header) + 64 + num_elements * plan.max_element_size));
		plan.hdr->element_size = plan.max_element_size;
	}

	// Link into gwnum's list of arrays to free when gwdone is called
	plan.hdr->linkage.next = pmdata->gwdata->array_list;		// Next in linked list of allocated arrays
	plan.hdr->linkage.prev = &pmdata->gwdata->array_list;	// Prev in linked list of allocated arrays
	if (plan.hdr->linkage.next != NULL) {			// Patch new next's prev pointer
		gwarray_header *next_header = (gwarray_header *) ((char *) plan.hdr->linkage.next - sizeof (gwarray_header));
		next_header->prev = &plan.hdr->linkage.next;
	}
	pmdata->gwdata->array_list = (gwarray) &plan.hdr->self_ptr;	// New head of doubly linked list

	// Return the address of the self_ptr that identifies this as a preprocessed poly
	plan.hdr->self_ptr = (gwnum *) &plan.hdr->self_ptr;
	return ((gwnum *) &plan.hdr->self_ptr);
}

/* Multiply two polynomials producing FFTed gwnums as output.  The output gwnums need to be normalized before use in further gwnum operations. */
/* It is safe to use input gwnums in the output vector.  For a normal polynomial multiply outvec_size must be invec1_size + invec2_size - 1. */
/* For monic polynomial multiply, the leading input coefficients of 1 are omitted as is the leading 1 output coefficient -- thus */
/* outvec_size must be invec1_size + invec2_size. */
void polymult (
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// First input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	gwnum	*invec2,		// Second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	gwnum	*outvec,		// Output poly
	uint64_t outvec_size,		// Size of the output polynomial (or fft_size if POLYMULT_CIRCULAR)
	int	options)
{
	polymult_arg arg;

	// POLYMULT_CIRCULAR and (POLYMULT_MULHI or POLYMUL_MULMID or POLYMULT_MULLO) cannot both be set -- it overloads the meaning of outvec_size.
	ASSERTG (!(options & POLYMULT_CIRCULAR) || !(options & (POLYMULT_MULHI | POLYMULT_MULMID | POLYMULT_MULLO)));
	// FMA options cannot be set
	ASSERTG (!(options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)));

	arg.invec2 = invec2;
	arg.invec2_size = invec2_size;
	arg.outvec = outvec;
	arg.outvec_size = outvec_size;
	arg.fmavec = NULL;
	arg.circular_size = (options & POLYMULT_CIRCULAR) ? outvec_size : 0;
	arg.first_mulmid = 0;
	arg.options = options & (INVEC2_OPTIONS | OUTVEC_OPTIONS);

	polymult_several (pmdata, invec1, invec1_size, &arg, 1, options & (PLAN_OPTIONS | INVEC1_OPTIONS));
}

   
/* Multiply two polynomials followed by a fused add or subtract */
void polymult_fma (
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// First input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	gwnum	*invec2,		// Second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	gwnum	*outvec,		// Output poly
	uint64_t outvec_size,		// Size of the output polynomial (or fft_size if POLYMULT_CIRCULAR)
	gwnum	*fmavec,		// FMA poly to add or subtract from poly multiplication result.  Same size as outvec, cannot be preprocessed.
	int	options)
{
	polymult_arg arg;

	// POLYMULT_CIRCULAR and (POLYMULT_MULHI or POLYMUL_MULMID or POLYMULT_MULLO) cannot both be set -- it overloads the meaning of outvec_size.
	ASSERTG (!(options & POLYMULT_CIRCULAR) || !(options & (POLYMULT_MULHI | POLYMULT_MULMID | POLYMULT_MULLO)));
	// FMA options must be set and fmavec cannot be NULL
	ASSERTG ((options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) && fmavec != NULL);

	arg.invec2 = invec2;
	arg.invec2_size = invec2_size;
	arg.outvec = outvec;
	arg.outvec_size = outvec_size;
	arg.fmavec = fmavec;
	arg.circular_size = (options & POLYMULT_CIRCULAR) ? outvec_size : 0;
	arg.first_mulmid = 0;
	arg.options = options & (INVEC2_OPTIONS | OUTVEC_OPTIONS);

	polymult_several (pmdata, invec1, invec1_size, &arg, 1, options & (PLAN_OPTIONS | INVEC1_OPTIONS));
}

/* Multiply two polynomials.  Supports every possible polymult option.  The original polymult interface overloaded the outvec_size argument. */
void polymult2 (
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// First input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	gwnum	*invec2,		// Second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	gwnum	*outvec,		// Output poly
	uint64_t outvec_size,		// Size of the output polynomial.  If POLYMULT_MULHI or POLYMULT_MULLO is set this is the number of coefficients to return.
	gwnum	*fmavec,		// FMA poly to add or subtract from poly multiplication result.  Same size as outvec, cannot be preprocessed.
	uint64_t circular_size,		// If POLYMULT_CIRCULAR set, compute poly result modulo (X^circular_size - 1)
	uint64_t first_mulmid,		// If POLYMULT_MULMID set, this is the number of least significant coefficients that will not be returned
	int	options)
{
	polymult_arg arg;

	// POLYMULT_CIRCULAR requires circular_size to be set
	ASSERTG (((options & POLYMULT_CIRCULAR) && circular_size > 0) || (!(options & POLYMULT_CIRCULAR) && circular_size == 0));
	// POLYMUL_MULMID requires first_mulmid be set
	ASSERTG (((options & POLYMULT_MULMID) && first_mulmid >= 0) || (!(options & POLYMULT_MULMID) && first_mulmid == 0));
	// FMA options must be set and fmavec cannot be NULL
	ASSERTG (((options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) && fmavec != NULL) ||
		 (!(options & (POLYMULT_FMADD | POLYMULT_FMSUB | POLYMULT_FNMADD)) && fmavec == NULL));

	arg.invec2 = invec2;
	arg.invec2_size = invec2_size;
	arg.outvec = outvec;
	arg.outvec_size = outvec_size;
	arg.fmavec = fmavec;
	arg.circular_size = circular_size;
	arg.first_mulmid = first_mulmid;
	arg.options = options & (INVEC2_OPTIONS | OUTVEC_OPTIONS);

	polymult_several (pmdata, invec1, invec1_size, &arg, 1, options & (PLAN_OPTIONS | INVEC1_OPTIONS));
}


/* Multiply one poly with several other polys.  This yields a small performance gain in that the one poly is read and FFTed just once. */
void polymult_several (		// Multiply one poly with several polys	
	pmhandle *pmdata,	// Handle for polymult library
	gwnum	*invec1,	// First input poly
	uint64_t invec1_size,	// Size of the first input polynomial
	polymult_arg *other_polys, // Array of "other poly descriptors" to multiply with first input poly (describes each second input poly and output poly)
	int	num_other_polys,// Number of other polys to multiply with first input poly
	int	options)	// Poly #1 options.  Options not associated with poly #1 are applied to all other polys.
{
	polymult_plan *plan;		// Plan for how to implement each of the invec1 by invec2 multiplications
	int	invec1_options;		// Options that only apply to invec1
	int	global_invec2_options;	// Options that apply to all invec2s

	// Split up options.  For convenience to the programmer, invec2 options that apply to all other_polys can be specified in options argument.
	invec1_options = options & INVEC1_OPTIONS;
	global_invec2_options = options & (INVEC2_OPTIONS | OUTVEC_OPTIONS);
	for (int i = 0; i < num_other_polys; i++) other_polys[i].options |= global_invec2_options;

	// Use a previously generated plan
	if (options & POLYMULT_USE_PLAN) {
	    plan = (polymult_plan *) pmdata->plan;
#define ps_hash(a,b,c,d,e,f) ((a)*3+(b)*17+(c)*131071+(d)*8191001+(e)*1001+(f)*65537)
	    ASSERTG (ps_hash (invec1_size, num_other_polys, other_polys[0].invec2_size, other_polys[0].outvec_size, other_polys[0].options & 0x7FFFF, options & 0x7FFFF) == ((polymult_plan *) pmdata->plan)->hash);
	}

	// Generate a new plan
	else {

	    // Allocate array to plan each poly multiplication
	    pmdata->plan = plan = (polymult_plan *) malloc (sizeof (polymult_plan) + (num_other_polys-1) * sizeof (struct planpart));
#ifdef GDEBUG
	    plan->hash = ps_hash (invec1_size, num_other_polys, other_polys[0].invec2_size, other_polys[0].outvec_size, other_polys[0].options & 0x7FFFF, options & 0x7FFFF);
#endif

	    // Check for pre-processed input poly
	    bool invec1_preprocessed = is_preprocessed_poly (invec1);
	    bool invec1_pre_ffted = invec1_preprocessed && is_preffted_poly (invec1);

	    // The default plan is to multi-thread polymult_line.  If fft_size is large we'll change to multi-threading fft_line_pass instead.
	    plan->mt_polymult_line = TRUE;

	    // For better or worse, if "other polys" would select different FFT sizes we try to make them all use the largest FFT size.  That way invec1 will
	    // be forward FFTed only once.  We also need to make sure the strip_monic_from_invec1 is consistent (invec1 is only read once and that is where
	    // monic ones are stripped).  To make this happen we track the largest FFT size and strip_monic_from_invec1 settings.  We way need to make two
	    // passes through the planning process to make this happen.
	    uint64_t largest_fft_size = 0;		// FFT size that we can use for all the other poly plans where we are free to choose the FFT size
	    bool largest_strip_monic_from_invec1;	// Whether invec1 has its monic one stripped when using largest_fft_size
	    bool must_replan;				// TRUE if replanning will be required
	    bool exists_pre_ffted_invec2 = FALSE;	// TRUE if any of the invec2s are pre-FFTed.  If so, we may not be able to strip monic ones from invec1.
	    bool pass2_strip_monic_from_invec1;		// In second pass, all other polys must use the same strip_monic_from_invec1 setting.

	    // Determine the best plan (brute/Karatsuba/FFT, FFT size, post_process_monic, 1*1 addins, etc.).  Sometimes this takes two passes.
	    for (int pass = 1; pass == 1 || must_replan; pass++) {
		must_replan = FALSE;

		// Track the largest buffer sizes that must be allocated
		plan->alloc_invec1_size = 0;
		plan->alloc_invec2_size = 0;
		plan->alloc_outvec_size = 0;
		plan->alloc_tmpvec_size = 0;

		// Loop over all the other polys to determine the plan (brute/Karatsuba/FFT, FFT size, post_process_monic, 1*1 addins, etc.) needed for each
		for (int i = 0; i < num_other_polys; i++) {
		    gwnum *invec2 = other_polys[i].invec2;		// Second input poly
		    uint64_t invec2_size = other_polys[i].invec2_size;	// Size of the second input polynomial
		    gwnum *outvec = other_polys[i].outvec;		// Output poly of size invec1_size + invec2_size - 1
		    uint64_t outvec_size = other_polys[i].outvec_size;	// Size of the output polynomial.  Should be invec1_size + invec2_size (less one if not monic).
		    gwnum *fmavec = other_polys[i].fmavec;		// Addin for FMA operations
		    uint64_t circular_size = other_polys[i].circular_size; // Return result modulo (X^circular_size - 1)
		    int	options;					// Merged invec1 and invec2 options to make life a little simpler
		    bool must_fft = FALSE;				// TRUE if there are any pre-FFTed poly inputs
		    uint64_t fft_size;

		    // Merge options into one variable.
		    options = invec1_options | other_polys[i].options;

		    // Don't allow specifying any invec1 or plan options in other poly array
		    ASSERTG (!(other_polys[i].options & (PLAN_OPTIONS | INVEC1_OPTIONS)));
		    // Cannot use combinations of POLYMULT_MULHI, POLYMULT_MULMID, and POLYMULT_MULLO
		    ASSERTG (!(options & POLYMULT_MULHI) || !(options & (POLYMULT_MULMID | POLYMULT_MULLO)));
		    ASSERTG (!(options & POLYMULT_MULMID) || !(options & (POLYMULT_MULHI | POLYMULT_MULLO)));
		    ASSERTG (!(options & POLYMULT_MULLO) || !(options & (POLYMULT_MULHI | POLYMULT_MULMID)));
		    // If POLYMULT_CIRCULAR option is consistent with circular_size setting
		    ASSERTG (((options & POLYMULT_CIRCULAR) && circular_size > 0) || (!(options & POLYMULT_CIRCULAR) && circular_size == 0));
		    // If first_mulmid can only be set when POLYMULT_MULMID is set
		    ASSERTG ((options & POLYMULT_MULMID) || other_polys[i].first_mulmid == 0);
		    // FMA vector cannot be pre-processed
		    ASSERTG (fmavec == NULL || !is_preprocessed_poly (fmavec));

//GW: Make this a subroutine that polymult_preprocess can use???

		    // Check for pre-processed other poly
		    bool invec2_preprocessed = is_preprocessed_poly (invec2);
		    bool invec2_pre_ffted = invec2_preprocessed && is_preffted_poly (invec2);
		    exists_pre_ffted_invec2 |= invec2_pre_ffted;

		    // Check for and validate pre-FFTed polys
		    if (invec1_pre_ffted) {
			must_fft = TRUE;
			fft_size = preprocessed_fft_size (invec1);
		    }
		    if (invec2_pre_ffted) {
			ASSERTG (!must_fft || fft_size == preprocessed_fft_size (invec2));
			must_fft = TRUE;
			fft_size = preprocessed_fft_size (invec2);
		    }

		    // Adjust sizes due to RLPs and monics.  Roughly speaking, the adjusted_XXX variables are for monics with their ones stripped thus requiring
		    // monic post processing, and the true_XXX variables are for monics handled by the poly FFT code.
		    uint64_t adjusted_invec1_size = (options & POLYMULT_INVEC1_RLP) ? 2 * invec1_size - 1 : invec1_size;
		    uint64_t adjusted_invec2_size = (options & POLYMULT_INVEC2_RLP) ? 2 * invec2_size - 1 : invec2_size;
		    uint64_t adjusted_outvec_size = adjusted_invec1_size + adjusted_invec2_size - 1;
		    uint64_t true_invec1_size = adjusted_invec1_size + ((options & POLYMULT_INVEC1_MONIC) ? ((options & POLYMULT_INVEC1_RLP) ? 2 : 1) : 0);
		    uint64_t true_invec2_size = adjusted_invec2_size + ((options & POLYMULT_INVEC2_MONIC) ? ((options & POLYMULT_INVEC2_RLP) ? 2 : 1) : 0);
		    uint64_t true_outvec_size = true_invec1_size + true_invec2_size - 1;

		    // We now know the true input and output vector sizes.  THIS WILL NOT CHANGE.  We may do a brute/Karatsuba/FFT multiply that is smaller
		    // than the true sizes and do some post-monic-addins to create the true result, but that will be reflected in other variables.
		    // Save the true sizes to the plan.
		    plan->planpart[i].true_invec1_size = true_invec1_size;
		    plan->planpart[i].true_invec2_size = true_invec2_size;
		    plan->planpart[i].true_outvec_size = true_outvec_size;

		    // If circular size is larger than the polymult result, then the modulo operation is a no-op.
		    if (circular_size >= true_outvec_size) options &= ~POLYMULT_CIRCULAR;
		    // Set circular_size even if POLYMULT_CIRCULAR option is not set.  This just makes later code simpler.
		    if (!(options & POLYMULT_CIRCULAR)) circular_size = true_outvec_size;

		    // Monic * monic is a mine field of edge cases.  We may have several options of including the monic one in the poly FFT data or using
		    // monic_line_adjustments after the polynomial multiplication.  Consider a monic-RLP times a monic-RLP, there are 4 possible implentations:
		    //
		    // Example:  1xxx1 * 1y1, impl1 is the obvious:  1xxx1*1y1
		    // Impl2 is:	(xxx*y)<<1 + 1xxx1<<2 + 1xxx1 + 1y1<<4 + 1y1 but don't add the ones in twice
		    // Impl3 is:	(1xxx1*y)<<1 + 1xxx1<<2 + 1xxx1
		    // Impl4 is:	(xxx*1y1)<<1 + 1y1<<4 + 1y1
		    //
		    // Furthermore, polymult must not calculate 1*1 because one is not a random number.  Consider a brute force multiplication of two
		    // polynomials that have one as a coefficient.  When two ones are multiplied together it adds the tiny signal FFT(1) to the result.
		    // This is a worst case scenario for round off error as it is easy for the FFT(1) signal to be "drowned out" by the summing of dozens
		    // or hundreds of multiplied signals of random numbers in the same "column".  There can be up to four 1*1 locations in the output
		    // vector (monic RLP * monic RLP).  Calculate the locations of these possible 1*1s.  These locations will need to be "patched" later
		    // and can affect the algorithm chosen to perform the polymult.
		    int64_t addin1, addin2, addin3, addin4, subout;	// Locations of the up to five 1*1 values may need to be patched later
		    addin1 = addin2 = addin3 = addin4 = subout = -1;	// Assume no addins or subouts of 1*1 are needed

		    // We also need to track which of the true_outvec_size coefficients will be returned.  For example, a monic * monic will not return the
		    // leading one.  A monic-RLP * monic-RLP returns a monic-RLP where the leading one and entire lower half of outvec are not returned.
		    // Also, the caller can request a specific number of the most significant or least significant coefficients using MULHI/MULMID/MULLO.
		    uint64_t LSWs_skipped;		// Number of least significant coefficients that will not be returned to the caller
		    uint64_t MSWs_skipped;		// Number of most significant coefficients that will not be returned to the caller

		    // In examples in the comments that follow, we show how two length two invecs can be processed

		    // The simplest case is two non-monic inputs giving a non-monic result
		    // Compute invec1 * invec2 as		(ab * cd)
		    if (!(options & POLYMULT_INVEC1_MONIC) && !(options & POLYMULT_INVEC2_MONIC)) {
			LSWs_skipped = 0;
			MSWs_skipped = 0;
		    }

		    // Next up are monic * non-monic cases giving a non-monic result
		    else if ((options & POLYMULT_INVEC1_MONIC) && !(options & POLYMULT_INVEC2_MONIC)) {
			// monic-RLP * non-monic
			// Compute invec1 * invec2 as		(aba * cd)		OR	(1aba1 * cd)
			// Then post_monic_add			(aba * cd)<<1 + cd<<4 + cd
			if (options & POLYMULT_INVEC1_RLP) {
				LSWs_skipped = 0;
				MSWs_skipped = 0;
			}
			// monic * non-monic
			// Compute invec1 * invec2 as		(ab * cd)		OR	(1ab * cd)
			// Then post_monic_add			(ab * cd) + cd<<2
			else {
				LSWs_skipped = 0;
				MSWs_skipped = 0;
			}
		    }

		    // and similarly the non-monic * monic cases giving a non-monic result
		    else if (!(options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC2_MONIC)) {
			// non-monic * monic-RLP
			// Compute invec1 * invec2 as		(ab * cdc)		OR	(ab * 1cdc1)
			// Then post_monic_add			(ab * cdc)<<1 + ab<<4 + ab
			if (options & POLYMULT_INVEC2_RLP) {
				LSWs_skipped = 0;
				MSWs_skipped = 0;
			}
			// non-monic * monic
			// Compute invec1 * invec2 as		(ab * cd)		OR	(ab * 1cd)
			// Then post_monic_add			(ab * cd) + ab<<2
			else {
				LSWs_skipped = 0;
				MSWs_skipped = 0;
			}
		    }

		    // Both inputs are monic raising the possibility of forbidden 1*1 calculations
		    // The monic * monic case gives us a monic result unless POLYMULT_CIRCULAR is set (leading 1*1 not output unless POLYMULT_CIRCULAR is set)
		    // Compute invec1 * invec2 as			(ab * 1cd)		OR	(1ab * cd)		OR	(1ab * 1cd)
		    // Then post_monic_add				(ab * 1cd) + cd<<2	OR	(1ab * cd) + ab<<2
		    // One can also start with (ab * cd) and do two post monic adds:    (ab * cd) + cd<<2 + ab<<2
		    // Then post add the 1*1 if necessary
		    else if (!(options & POLYMULT_INVEC1_RLP) && !(options & POLYMULT_INVEC2_RLP)) {
			addin1 = true_outvec_size - 1;				// Location of the leading 1*1
			MSWs_skipped = 1;					// The leading 1*1 is not output
			LSWs_skipped = 0;
		    }

		    // The monic-RLP * monic-RLP case gives us a monic-RLP result unless POLYMULT_CIRCULAR is set
		    // Compute invec1 * invec2 as		(aba * 1cdc1)				OR	(1aba1 * cdc)
		    // Then post_monic_add			(aba * 1cdc1)<<1 + cdc<<5 + cdc		OR	(1aba1 * cdc)<<1 + aba<<5 + aba
		    // One can also start with (aba * cdc) and do four post monic adds:	(aba * cdc)<<2 + cdc<<5 + cdc + aba<<5 + aba
		    // Then post add two middle 1*1s OR post add four 1*1s if circular
		    else if ((options & POLYMULT_INVEC1_RLP) && (options & POLYMULT_INVEC2_RLP)) {
			addin1 = true_outvec_size - 1;				// Location of the leading 1*1
			addin2 = true_invec2_size - 1;				// Location of middle 1*1 (leading 1 of invec2 * trailing 1 of invec1)
			addin3 = true_invec1_size - 1;				// Location of middle 1*1 (leading 1 of invec1 * trailing 1 of invec2)
			addin4 = 0;						// Location of the trailing 1*1
			MSWs_skipped = 1;					// The leading 1*1 is not output
			LSWs_skipped = 0;					// This will be changed later
		    }

		    // The monic-RLP * monic case gives us a monic result unless POLYMULT_CIRCULAR is set
		    // Compute invec1 * invec2 as		(aba * 1cd)			OR	(1aba1 * cd)
		    // Then post_monic_add			(aba * 1cd)<<1 + cd<<4 + cd		(1aba1 * cd) + aba<<3
		    // One can also start with (aba * cd) and do three post monic adds:	(aba * cd)<<1 + cd<<4 + cd + aba<<3
		    // Then post add one middle 1*1 OR post add both 1*1s if circular
		    else if (!(options & POLYMULT_INVEC2_RLP)) {
			addin1 = true_outvec_size - 1;				// Location of the leading 1*1
			addin2 = true_invec2_size - 1;				// Location of middle 1*1 (leading 1 of invec2 * trailing 1 of invec1)
			MSWs_skipped = 1;					// The leading 1*1 is not output
			LSWs_skipped = 0;
		    }

		    // Similarly, the monic * monic-RLP case gives us a monic result unless POLYMULT_CIRCULAR is set
		    // Compute invec1 * invec2 as		(ab * 1cdc1)			OR	(1ab * cdc)
		    // Then post_monic_add			(ab * 1cdc1) + cdc<<3			(1ab * cdc)<<1 + ab<<4 + ab
		    // One can also start with (ab * cdc) and do three post monic adds:	(ab * cdc)<<1 + cdc<<3 + ab<<4 + ab
		    // Then post add one middle 1*1 OR post add both 1*1s if circular
		    else if (!(options & POLYMULT_INVEC2_RLP)) {
			addin1 = true_outvec_size - 1;				// Location of the leading 1*1
			addin3 = true_invec1_size - 1;				// Location of middle 1*1 (leading 1 of invec1 * trailing 1 of invec2)
			MSWs_skipped = 1;					// The leading 1*1 is not output
			LSWs_skipped = 0;
		    }

		    // The RLP * RLP case gives us a RLP result unless POLYMULT_CIRCULAR is set.  Do not return the half of the coefficients.
		    if ((options & POLYMULT_INVEC1_RLP) && (options & POLYMULT_INVEC2_RLP)) {
			LSWs_skipped = true_outvec_size / 2;			// The trailing reciprocal coefficients are not output
		    }

		    // For a circular polymult every coefficient is computed
		    if (options & POLYMULT_CIRCULAR) {
			MSWs_skipped = 0;
			LSWs_skipped = 0;
		    }

		    // Validate outvec size argument
		    ASSERTG ((!(options & (POLYMULT_MULHI | POLYMULT_MULMID | POLYMULT_MULLO)) && outvec_size == circular_size - MSWs_skipped - LSWs_skipped) ||
			     ((options & (POLYMULT_MULHI | POLYMULT_MULLO)) && outvec_size <= circular_size - MSWs_skipped - LSWs_skipped) ||
			     ((options & POLYMULT_MULMID) && outvec_size <= circular_size - MSWs_skipped - LSWs_skipped - other_polys[i].first_mulmid));

		    // Adjust the output words skipped based on caller's MULHI/MULMID/MULLO request
		    if (options & POLYMULT_MULHI) LSWs_skipped = circular_size - MSWs_skipped - outvec_size;
		    if (options & POLYMULT_MULMID) LSWs_skipped += other_polys[i].first_mulmid;
		    if (options & (POLYMULT_MULMID | POLYMULT_MULLO)) MSWs_skipped = circular_size - LSWs_skipped - outvec_size;
		    ASSERTG (circular_size == MSWs_skipped + outvec_size + LSWs_skipped);

		    // If emulating POLYMULT_CIRCULAR, apply modulo to every addin location
		    if (addin1 >= (int64_t) circular_size) addin1 %= circular_size;
		    if (addin2 >= (int64_t) circular_size) addin2 %= circular_size;
		    if (addin3 >= (int64_t) circular_size) addin3 %= circular_size;
		    if (addin4 >= (int64_t) circular_size) addin4 %= circular_size;

		    // Make the 1*1 addins an index into outvec rather than the current index into true_outvec.  That is, adjust for LSWs skipped.
		    // We do this because 1*1 addins are added as the very last step before returning outvec.
		    addin1 -= LSWs_skipped;
		    addin2 -= LSWs_skipped;
		    addin3 -= LSWs_skipped;
		    addin4 -= LSWs_skipped;

		    // If any of the 1*1s are for a coefficient that is not returned to the caller, then clear the addin flag
		    if (addin1 >= 0 && (addin1 >= (int64_t) outvec_size || outvec[addin1] == NULL)) addin1 = -1;
		    if (addin2 >= 0 && (addin2 >= (int64_t) outvec_size || outvec[addin2] == NULL)) addin2 = -1;
		    if (addin3 >= 0 && (addin3 >= (int64_t) outvec_size || outvec[addin3] == NULL)) addin3 = -1;
		    if (addin4 >= 0 && (addin4 >= (int64_t) outvec_size || outvec[addin4] == NULL)) addin4 = -1;

		    // Choose a brute force implementation if we are not returning many coefficients or one of the input polys is very small
		    // NOTE: No studies have been done regarding a close-to-optimal definition of "very small".  Brute force handles addins directly.
		    bool	will_brute_force;		// TRUE if we're going to choose a brute force implementation
		    will_brute_force = !must_fft && (true_invec1_size < pmdata->KARAT_BREAK || true_invec2_size < pmdata->KARAT_BREAK);
		    // Having brute force add in the 1*1 values is very dangerous.  This led to P-1 problems in versions 30.10 to 30.12 on large exponents
		    // when bits-per-word > 16.1.  We could allow this optimization when EXTRA_BITS is big enough, but we'd need to be very careful in
		    // future top coefficient optimizations of unfft.
		    // if (will_brute_force) addin1 = addin2 = addin3 = addin4 = -1;

		    // Save addins and skipped outputs in the plan
		    plan->planpart[i].addin[0] = addin1;
		    plan->planpart[i].addin[1] = addin2;
		    plan->planpart[i].addin[2] = addin3;
		    plan->planpart[i].addin[3] = addin4;
		    plan->planpart[i].LSWs_skipped = LSWs_skipped;
		    plan->planpart[i].MSWs_skipped = MSWs_skipped;

		    // If any 1*1s affect returned data then one (or both) of the monic polys must be stripped of their one coefficients and results
		    // patched as a final step.  Prefer to strip the one coefficients of the longer input (so we patch by adding in the shorter input).
		    // We track whether stripping ones from a monic invec is required.  Plus track if either invec can have its ones stripped without requiring
		    // monic post processing (very rare, but an early prime95 P-1 implementation actually did this).
		    bool must_strip_a_monic;			// TRUE if at least one of the two invecs must have its monic ones stripped to avoid a dangerous 1*1
		    bool invec1_can_be_stripped_cheaply;	// TRUE if invec1 can have its ones stripped without requiring monic post processing
		    bool invec2_can_be_stripped_cheaply;	// TRUE if invec1 can have its ones stripped without requiring monic post processing
		    bool strip_monic_from_invec1, strip_monic_from_invec2, post_monic_addin_invec1, post_monic_addin_invec2;

		    // If any 1*1 calculations land in a returned coefficient, then one of the monics must have its ones stripped.
		    must_strip_a_monic = (addin1 >= 0 || addin2 >= 0 || addin3 >= 0 || addin4 >= 0);

		    // Stripping the ones from a monic normally requires post processing that adds the other invec to the result.  However, if all the non-one
		    // coefficients in the other invec land in columns that are not returned, then the post processing step can be skipped (at most just the
		    // final 1*1 addin is needed to create the correct result).
		    invec1_can_be_stripped_cheaply = (options & POLYMULT_INVEC1_MONIC) &&
				true_invec2_size - (((options & POLYMULT_INVEC2_MONIC) && (options & POLYMULT_INVEC2_RLP)) ? 1 : 0) <= MSWs_skipped &&
				(!(options & POLYMULT_INVEC1_RLP) || true_invec2_size - ((options & POLYMULT_INVEC2_MONIC) ? 1 : 0) <= LSWs_skipped);
		    invec2_can_be_stripped_cheaply = (options & POLYMULT_INVEC2_MONIC) &&
				true_invec1_size - (((options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC1_RLP)) ? 1 : 0) <= MSWs_skipped &&
				(!(options & POLYMULT_INVEC2_RLP) || true_invec1_size - ((options & POLYMULT_INVEC1_MONIC) ? 1 : 0) <= LSWs_skipped);

		    // If a pre-FFTed input had its monic ones stripped then we obviously must strip a monic
		    must_strip_a_monic |= (invec1_pre_ffted && !preprocessed_monics_included (invec1));
		    must_strip_a_monic |= (invec2_pre_ffted && !preprocessed_monics_included (invec2));

		    // Under ideal conditions, a circular polymult can be done for free by the poly FFT.  Otherwise it must be emulated.
		    // Post-monic processing can handle a circular adjusted polymult result, but handling a shifted circular result would be tricky (invecs would
		    // need shifting or don't delete the trailing ones, brute_line would need changing).  Since this tricky combination only occurs in the rare
		    // monic * monic-RLP case, we simply force emulating circular size.
		    bool can_use_circular_fft_size = FALSE;		// TRUE if using circular_size smaller than the true_outvec_size as poly FFT size will work
		    bool can_use_circular_if_monic_stripped = FALSE;	// TRUE if using circular_size smaller than the true_outvec_size works if a monic is stripped
		    uint64_t circular_fft_size;
		    if (options & POLYMULT_CIRCULAR) {
			// Verify that a pre-FFTed input vector is compatible with the circular size
			ASSERTG (!must_fft || (fft_size >= circular_size && (fft_size <= circular_size + LSWs_skipped + 1 || fft_size >= true_outvec_size)));
			// Calculate polymult FFT size we hope we can use for a free POLYMULT_CIRCULAR operation
			circular_fft_size = must_fft ? fft_size : polymult_fft_size (circular_size);
			// We can use circular_fft_size for a free POLYMULT_CIRCULAR when circular_fft_size equals circular size OR when the wrapped coefficients
			// pollute only the LSW_skipped coefficients (i.e. none of wrapped coefficients belonged in the final result).
			// This means true_outvec_size - circular_size <= LSWs_skipped
			// If we strip monics we can let one wrapped coefficient be in the final result: true_outvec_size - circular_size <= LSWs_skipped + 1
			if (circular_size >= true_invec1_size && circular_size >= true_invec2_size &&
			    circular_fft_size < true_outvec_size &&
			    (circular_fft_size == circular_size ||
			     (true_outvec_size - circular_size <= LSWs_skipped && !must_strip_a_monic) ||
			     (true_outvec_size - circular_size <= LSWs_skipped + 1 && (options & (POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC)) &&
			      !((options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC2_MONIC) && (options & (POLYMULT_INVEC1_RLP | POLYMULT_INVEC2_RLP)))))) {
				can_use_circular_fft_size = TRUE;
				can_use_circular_if_monic_stripped = !(circular_fft_size == circular_size ||
								       (true_outvec_size - circular_size <= LSWs_skipped && !must_strip_a_monic));
			}
		    }

		    // If some of the coefficients are not returned, we can use a smaller poly FFT size as the long as the wrapped
		    // coefficients pollute the coefficients that are not being returned.  For example, consider a true_outvec_size of 18:
		    // 4 MSWs skipped, 10 returned, 4 LSWs skipped => A circular mult=14 works (0 MSWs skipped, 10 returned, 4 LSWs skipped)
		    // 3 MSWs skipped, 10 returned, 5 LSWs skipped => A circular mult=15 works (0 MSWs skipped, 10 returned, 5 LSWs skipped)
		    // 5 MSWs skipped, 10 returned, 3 LSWs skipped => A circular mult=15 works (2 MSWs skipped, 10 returned, 3 LSWs skipped)
		    //GW NOTE:  We can do one better by writing monic_addin code that understands MSWs skipped.  That is a monic size9 * non-monic size9 (true_size=18)
		    // 5 MSWs skipped, 9 returned, 4 LSWs skipped => Strip the monic9 and use circular mult=13 (0 MSWs skipped, 9 returned, 4 LSWs skipped
		    // with only the lower 4 coefficients of the non-monic size9 added back in to the 9 returned coefficeints)
		    else if (MSWs_skipped > 0 && LSWs_skipped > 0 && (!must_fft || fft_size < true_outvec_size)) {
			uint64_t savings = (LSWs_skipped <= MSWs_skipped) ? LSWs_skipped : MSWs_skipped;
			circular_fft_size = polymult_fft_size (true_outvec_size - savings);
			if (must_fft && fft_size > circular_fft_size) circular_fft_size = fft_size;
			if (circular_fft_size < true_outvec_size) can_use_circular_fft_size = TRUE;
//GW: Pre-fft knows nothing about this optimization -- need a method for pre-fft to output this smaller circular size??
		    }

		    // Figure out how we want to handle monics.  If implementation is POLYMULT_IMPL_FFT, post processing monics is an O(N) cost whereas
		    // having the poly FFT do some or all of the monic work is free --- as long as it does not require a larger poly FFT size.
		    // For other implementations, the choice between post-processing monics or handling as part of brute or Karatsuba is pretty much irrelevant.
		    int	best_choice = 0;
		    uint64_t best_fft_size = 0;
		    uint64_t best_poly_addin_size = 0;

		    // There are 4 possible monic implementations.  1) All monic ones included in FFT data.  2) Monic ones stripped from invec2 requiring invec1
		    // to be added with post processing.  3) Monic ones stripped from invec1 requiring invec2 to be added with post processing.  4) Monic ones
		    // stripped from both invec1 and invec2 requiring invec1 and invec2 to be added with post processing.
		    // Select the implementation that gives us the smallest poly FFT size and fewest number of post-processing adds.

//GW: best monic impl may well depend on how circular_size is implemented.  worrying too much about circular impl with monic addins is fairly pointless

//GW:  Does code below work well for fft_size < FFT_BREAK?  we'll be selecting brute force impl.  Not sure if post_process is slower or faster.
//GW:  This is especially important for detecting different strip_monic_invec1 settings

		    // If handling monics without stripping ones is possible, make that our best poly fft choice thusfar
		    if (!must_strip_a_monic && (pass == 1 || !pass2_strip_monic_from_invec1)) {
			uint64_t possible_fft_size = polymult_fft_size (true_outvec_size);
			if (can_use_circular_fft_size && !can_use_circular_if_monic_stripped && circular_fft_size < possible_fft_size) possible_fft_size = circular_fft_size;
			if (must_fft && fft_size > possible_fft_size) possible_fft_size = fft_size;
			if (!must_fft && pass == 2 && possible_fft_size < largest_fft_size) possible_fft_size = largest_fft_size;
			if (!must_fft || possible_fft_size <= fft_size) {
				best_choice = 1;
				best_fft_size = possible_fft_size;
				best_poly_addin_size = 0;
				// Skip looking for smaller FFT size if we're going to choose brute force implementation
				if (will_brute_force) goto use_it;
			}
		    }

		    // An experimental case where we allow a single 1*1 calculation that we think will not trigger roundoff errors.  If wrapping just the 1*1
		    // in a monic times monic polymult results in a smaller FFT size, then use the smaller FFT size and subtract 1*1 from the least significant
		    // coefficient.  We think this will be OK because the poly FFT will be adding the 1*1 to a single subproduct (LSW-of-invec1 * LSW-of-invec2).
		    if (!must_strip_a_monic && (pass == 1 || !pass2_strip_monic_from_invec1) &&
		    (options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC2_MONIC) && LSWs_skipped == 0) {
			uint64_t possible_fft_size = polymult_fft_size (true_outvec_size - 1);
			if ((must_fft || possible_fft_size >= pmdata->FFT_BREAK) &&
			    (best_choice == 0 || possible_fft_size < best_fft_size) &&
			    (pass == 1 || possible_fft_size == largest_fft_size) &&
			    (!must_fft || (must_fft && fft_size == possible_fft_size))) {
				best_choice = 2;
				best_fft_size = possible_fft_size;
				best_poly_addin_size = 0;
			}
		    }

		    // If handling monics by removing ones from invec2 and post-adding invec1 is possible, then see if that is a better choice
		    if ((options & POLYMULT_INVEC2_MONIC) &&
			(pass == 1 || !pass2_strip_monic_from_invec1) &&
			(!invec2_pre_ffted || !preprocessed_monics_included (invec2)) &&
			(invec2_can_be_stripped_cheaply || !invec1_pre_ffted)) {
			uint64_t possible_fft_size = polymult_fft_size (true_outvec_size - (true_invec2_size - adjusted_invec2_size));
			uint64_t possible_poly_addin_size = (invec2_can_be_stripped_cheaply ? 0 : adjusted_invec1_size);
			if (can_use_circular_fft_size && circular_fft_size < possible_fft_size) possible_fft_size = circular_fft_size;
			if (!must_fft && pass == 2 && possible_fft_size < largest_fft_size) possible_fft_size = largest_fft_size;
			if (must_fft && fft_size > possible_fft_size) possible_fft_size = fft_size;
			if ((!must_fft || possible_fft_size <= fft_size) &&
			    (best_choice == 0 || possible_fft_size < best_fft_size ||
			     (possible_fft_size == best_fft_size && possible_poly_addin_size < best_poly_addin_size))) {
				best_choice = 3;
				best_fft_size = possible_fft_size;
				best_poly_addin_size = possible_poly_addin_size;
			}
		    }

		    // If handling monics by removing ones from invec1 and post-adding invec2 is possible, then see if that is a better choice
		    if ((options & POLYMULT_INVEC1_MONIC) &&
			(pass == 1 || pass2_strip_monic_from_invec1) &&
			(!invec1_pre_ffted || !preprocessed_monics_included (invec1)) &&
			(invec1_can_be_stripped_cheaply || !invec2_pre_ffted)) {
			uint64_t possible_fft_size = polymult_fft_size (true_outvec_size - (true_invec1_size - adjusted_invec1_size));
			uint64_t possible_poly_addin_size = (invec1_can_be_stripped_cheaply ? 0 : adjusted_invec2_size);
			if (can_use_circular_fft_size && circular_fft_size < possible_fft_size) possible_fft_size = circular_fft_size;
			if (!must_fft && pass == 2 && possible_fft_size < largest_fft_size) possible_fft_size = largest_fft_size;
			if (must_fft && fft_size > possible_fft_size) possible_fft_size = fft_size;
			if ((!must_fft || possible_fft_size <= fft_size) &&
			    (best_choice == 0 || possible_fft_size < best_fft_size ||
			     (possible_fft_size == best_fft_size && possible_poly_addin_size < best_poly_addin_size))) {
				best_choice = 4;
				best_fft_size = possible_fft_size;
				best_poly_addin_size = possible_poly_addin_size;
			}
		    }

		    // If handling both monics by removing ones is possible, then see if that is a better choice
		    if ((options & POLYMULT_INVEC1_MONIC) && (options & POLYMULT_INVEC2_MONIC) &&
			(pass == 1 || pass2_strip_monic_from_invec1) &&
			(!invec1_pre_ffted || !preprocessed_monics_included (invec1)) &&
			(!invec2_pre_ffted || !preprocessed_monics_included (invec2)) &&
			(invec1_can_be_stripped_cheaply || !invec2_pre_ffted) &&
			(invec2_can_be_stripped_cheaply || !invec1_pre_ffted)) {
			uint64_t possible_fft_size = polymult_fft_size (adjusted_outvec_size);
			uint64_t possible_poly_addin_size = (invec1_can_be_stripped_cheaply ? 0 : adjusted_invec2_size) +
							    (invec2_can_be_stripped_cheaply ? 0 : adjusted_invec1_size);
			if (can_use_circular_fft_size && circular_fft_size < possible_fft_size) possible_fft_size = circular_fft_size;
			if (!must_fft && pass == 2 && possible_fft_size < largest_fft_size) possible_fft_size = largest_fft_size;
			if (must_fft && fft_size > possible_fft_size) possible_fft_size = fft_size;
			if ((!must_fft || possible_fft_size <= fft_size) &&
			    (best_choice == 0 || possible_fft_size < best_fft_size ||
			     (possible_fft_size == best_fft_size && possible_poly_addin_size < best_poly_addin_size))) {
				best_choice = 5;
				best_fft_size = possible_fft_size;
				best_poly_addin_size = possible_poly_addin_size;
			}
		    }

		    // Make sure one of the 5 implementations was possible
use_it:		    ASSERTG (best_choice != 0);

		    // Set flags and sizes based on the best monic implementation determined above
		    if (best_choice == 1) {		// Monics handled without post processing
			strip_monic_from_invec1 = post_monic_addin_invec2 = FALSE;
			strip_monic_from_invec2 = post_monic_addin_invec1 = FALSE;
			adjusted_invec1_size = true_invec1_size;
			adjusted_invec2_size = true_invec2_size;
		    }
		    if (best_choice == 2) {		// Monics handled without post processing by wrapping a 1*1 and later subtracting it back out
			strip_monic_from_invec1 = post_monic_addin_invec2 = FALSE;
			strip_monic_from_invec2 = post_monic_addin_invec1 = FALSE;
			adjusted_invec1_size = true_invec1_size;
			adjusted_invec2_size = true_invec2_size;
			circular_size = best_fft_size;
			subout = 0;			// Subtract one from least significant coefficient
		    }
		    if (best_choice == 3) {		// Monics handled by strippng ones from invec2 and usually adding invec1 during post processing
			strip_monic_from_invec1 = post_monic_addin_invec2 = FALSE;
			strip_monic_from_invec2 = TRUE; post_monic_addin_invec1 = !invec2_can_be_stripped_cheaply;
			adjusted_invec1_size = true_invec1_size;
		    }
		    if (best_choice == 4) {		// Monics handled by strippng ones from invec1 and usually adding invec2 during post processing
			strip_monic_from_invec1 = TRUE; post_monic_addin_invec2 = !invec1_can_be_stripped_cheaply;
			strip_monic_from_invec2 = post_monic_addin_invec1 = FALSE;
			adjusted_invec2_size = true_invec2_size;
		    }
		    if (best_choice == 5) {		// Monics handled by stripping ones from both invecs
			strip_monic_from_invec1 = TRUE; post_monic_addin_invec2 = !invec1_can_be_stripped_cheaply;
			strip_monic_from_invec2 = TRUE; post_monic_addin_invec1 = !invec2_can_be_stripped_cheaply;
		    }
		    adjusted_outvec_size = adjusted_invec1_size + adjusted_invec2_size - 1;

		    // Make sure every "other poly" uses the same FFT size if possible.  In pass 1 we find the largest FFT size.
		    // This is not optimal but not worth optimizing, after all how often will someone want to have many other polys of different size!
		    // Optimal would likely order the other polys by FFT size and then minimize the number of FFT sizes used.
		    if (pass == 1) {
			if (best_fft_size != largest_fft_size) {
				// We need a second pass if we encounter multiple FFT sizes in the first pass
				if (largest_fft_size != 0) must_replan = TRUE;
				// Remember the largest FFT size encountered
				if (best_fft_size > largest_fft_size) {
					largest_fft_size = best_fft_size;
					largest_strip_monic_from_invec1 = strip_monic_from_invec1;
				}
			} else {
				// Remember if any usage of the largest FFT size requires stripping monics from invec1
				if (strip_monic_from_invec1 != largest_strip_monic_from_invec1) {
					largest_strip_monic_from_invec1 = TRUE;
					must_replan = TRUE;
				}
			}
		    }
		    // In pass 2, we should be using the largest FFT size whenever possible
		    ASSERTG (pass == 1 || must_fft || best_fft_size == largest_fft_size);

		    // Choose a brute force implementation if we are not returning many coefficients or one of the input polys is very small
		    // NOTE: No studies have been done regarding a close-to-optimal definition of "very small".
		    if (will_brute_force) {
			plan->planpart[i].impl = POLYMULT_IMPL_BRUTE;
		    }
		    // Choose Karatsuba
		    else if (!must_fft && !can_use_circular_fft_size && adjusted_outvec_size < pmdata->FFT_BREAK) {
			plan->planpart[i].impl = POLYMULT_IMPL_KARATSUBA;
		    }
		    // FFT implementation.  There may be several possible FFT sizes to choose from.  We prefer to choose the smallest.
		    else {
			plan->planpart[i].impl = POLYMULT_IMPL_FFT;
			plan->planpart[i].fft_size = fft_size = best_fft_size;
			int complex_vector_size = complex_vector_size_in_bytes (pmdata->gwdata->cpu_flags);
			if (pmdata->num_threads > 1 && fft_size >= pmdata->mt_ffts_start && fft_size < pmdata->mt_ffts_end)
				plan->mt_polymult_line = FALSE;
		    }
		    // Use streamed stores.  Generally, set this if the output polynomial uses more memory than the L3 cache size or if POLYMULT_NO_UNFFT is set.
		    plan->planpart[i].streamed_stores = (fft_size >= pmdata->streamed_stores_start);

		    // If we selected the circular fft size, then save some necessary plan variables
		    if (can_use_circular_fft_size && plan->planpart[i].impl == POLYMULT_IMPL_FFT && fft_size == circular_fft_size) {
			circular_size = circular_fft_size;
//GW: Pre-fft knows nothing about this optimization -- need a method for pre-fft to output this smaller circular size
//GW: Rotation not allowed if both inputs pre-ffted
			// Verify that a pre-FFTed input vector is compatible with the circular size
//GW			ASSERTG (!must_fft || fft_size >= circular_size);
		    }

		    // Calculate how much to pad the adjusted polymult result.  Padding may be required on the left and right to reach true_outvec_size.
//		    if (true_outvec_size != circular_size) {
//GW: BUG????  are all cases handled properly?? do this before the brute/karat/fft above? if poly FFT is implementing circular s.b. 0,0???
		    plan->planpart[i].adjusted_pad = plan->planpart[i].adjusted_shift = 0;
		    if (post_monic_addin_invec1) {
			plan->planpart[i].adjusted_pad++;					// Padding on the left
			if (options & POLYMULT_INVEC2_RLP) plan->planpart[i].adjusted_shift++;	// Padding on the right
		    }
		    if (post_monic_addin_invec2) {
			plan->planpart[i].adjusted_pad++;					// Padding on the left
			if (options & POLYMULT_INVEC1_RLP) plan->planpart[i].adjusted_shift++;	// Padding on the right
		    }

		    // Save adjusted sizes and monic addins in the plan
		    plan->planpart[i].adjusted_invec1_size = adjusted_invec1_size;
		    plan->planpart[i].adjusted_invec2_size = adjusted_invec2_size;
		    plan->planpart[i].adjusted_outvec_size = adjusted_outvec_size;
		    plan->planpart[i].post_monic_addin_invec1 = post_monic_addin_invec1;
		    plan->planpart[i].post_monic_addin_invec2 = post_monic_addin_invec2;
		    plan->planpart[i].strip_monic_from_invec1 = strip_monic_from_invec1;
		    plan->planpart[i].strip_monic_from_invec2 = strip_monic_from_invec2;
		    plan->planpart[i].subout = subout;

		    // Set plan's emulate_circular flag.  Brute force and ideal circular FFT size both implement circular_size for free.
		    plan->planpart[i].emulate_circular = ((options & POLYMULT_CIRCULAR) && circular_size != true_outvec_size &&
							  ((plan->planpart[i].impl == POLYMULT_IMPL_FFT && circular_size != fft_size) ||
							   plan->planpart[i].impl == POLYMULT_IMPL_KARATSUBA));
		    plan->planpart[i].circular_size = circular_size;

//GW:			when given a choice pre-process should include the ones unless it forces a larger poly FFT size.
//GW:			unless poly 2 will be pre-ffted, then we MUST include the ones  (need a preprocess option for that? POLYMULT_OTHER_POLY_PRE_FFTED?)

//GW  ASSERT if pre-ffted invec1 or invec2 is the wrong FFT size (or if we have a choice of sizes and pre-ffted size is one of them then use it)

//GW: POST PROCESS to find cases where different FFT sizes are used and and switch all to larger one.  Could happen in non-power-of-two tree, but likely very rare:
//gw: tree == 769 breaks down to 385 and 384 length.  It's not clear that using fftlen=385(rounds up to 512) for both would be faster    

		    // Track the largest buffer sizes that must be allocated
		    if (plan->planpart[i].impl == POLYMULT_IMPL_BRUTE) {
			if (adjusted_invec1_size > plan->alloc_invec1_size) plan->alloc_invec1_size = adjusted_invec1_size;
			if (adjusted_invec2_size > plan->alloc_invec2_size) plan->alloc_invec2_size = adjusted_invec2_size;
			if (true_outvec_size > plan->alloc_outvec_size) plan->alloc_outvec_size = true_outvec_size;
		    }
		    else if (plan->planpart[i].impl == POLYMULT_IMPL_KARATSUBA) {
			if (adjusted_invec1_size > plan->alloc_invec1_size) plan->alloc_invec1_size = adjusted_invec1_size;
			if (adjusted_invec2_size > plan->alloc_invec2_size) plan->alloc_invec2_size = adjusted_invec2_size;
			if (true_outvec_size > plan->alloc_outvec_size) plan->alloc_outvec_size = true_outvec_size;
			if (adjusted_outvec_size + 32 > plan->alloc_tmpvec_size) plan->alloc_tmpvec_size = adjusted_outvec_size + 32; // May need extra for each recursion
		    }
		    else {
			// Invec1 will be read into tmpvec unless post_monic_addin_invec1 is set or multiple FFT sizes/stripping will be used
			if ((post_monic_addin_invec1 || fft_size != largest_fft_size || strip_monic_from_invec1 != largest_strip_monic_from_invec1) &&
			    true_invec1_size > plan->alloc_invec1_size)
				plan->alloc_invec1_size = true_invec1_size;
			// The FFT of invec1 will be stored in tmpvec
			if (fft_size > plan->alloc_tmpvec_size) plan->alloc_tmpvec_size = fft_size;
			// Invec2 will be read into outvec unless post_monic_addin_invec2 is set
			if (post_monic_addin_invec2 && true_invec2_size > plan->alloc_invec2_size) plan->alloc_invec2_size = true_invec2_size;
			// The FFT of invec2 as well as the polymult result will be stored in outvec
			if (fft_size + plan->planpart[i].adjusted_pad + plan->planpart[i].adjusted_shift > plan->alloc_outvec_size)
				plan->alloc_outvec_size = fft_size + plan->planpart[i].adjusted_pad + plan->planpart[i].adjusted_shift;
		    }
		}

		// For the optional second pass, if any of the times we selected the largest FFT size required stripping monic one from invec1, then always strip.
		pass2_strip_monic_from_invec1 = largest_strip_monic_from_invec1;
		// For the optional second pass, if any of the other polys were pre-FFTed then assume we cannot strip monic one from invec1.
		// If using the largest FFT size required stripping monics then we will have to use the next larger FFT size.
		if (exists_pre_ffted_invec2) {
		    if (largest_strip_monic_from_invec1) largest_fft_size = polymult_fft_size (largest_fft_size + 1);
		    pass2_strip_monic_from_invec1 = FALSE;
		}
	    }
	}
//GW		Change poly_preprocess to either fft_size of polymult_fft_size(insz1 + insz2 - 1) OR polymult_fft_size (outsz) if polymult circular set.
//GW		This offloads some of this monic decision crap to the caller.  And lets him manage the exact fft size in case we would have decided wrong fft size.

	// Prepare for polymult in parallel
	pmdata->invec1 = invec1;
	pmdata->invec1_size = invec1_size;
	pmdata->other_polys = other_polys;
	pmdata->num_other_polys = num_other_polys;
	pmdata->options = invec1_options;
	pmdata->mt_polymult_line = plan->mt_polymult_line;

	// FFT all the inputs
	pmdata->helper_opcode = HELPER_PRE_FFT;		// The helpers are doing pre-FFT polymult work
	polymult_launch_helpers (pmdata);

	// Fire up each helper thread to do the real polymult work
	pmdata->helper_opcode = HELPER_POLYMULT_LINE;	// The helpers are doing polymult work
	polymult_launch_helpers (pmdata);

	// For the no-unfft optimization of top output coefficient remember the unnormalized add count of top invec coefficients (before polymult_post_unfft
	// might overwrite the value during an in-place polymult).
	plan->unnorms1 = is_preprocessed_poly (invec1) ? preprocessed_top_unnorms (invec1) : unnorms (invec1[invec1_size-1]);
	for (int i = 0; i < num_other_polys; i++) {
		gwnum *invec2 = other_polys[i].invec2;			// Second input poly
		uint64_t invec2_size = other_polys[i].invec2_size;	// Size of the second input polynomial
		plan->planpart[i].unnorms2 = is_preprocessed_poly (invec2) ? preprocessed_top_unnorms (invec2) : unnorms (invec2[invec2_size-1]);
	}

	// unFFT all the outputs (unless requested not to)
//GW: We would LIKELY be better off using no helpers if POLYMULT_UNFFT is set and there are any post-addins required.  This way the 1 or 2 gwunfft2s required
// for addin would use gwnum multi-threading (rather than a clone gwnum with a single thread).  Also if NO_UNFFT is set it might be faster to loop through all
// the coefficients without the overhead invoking all the launch and sync machinery.
	pmdata->helper_opcode = HELPER_POST_UNFFT;	// The helpers are doing post-unFFT polymult work
	polymult_launch_helpers (pmdata);

	// Free array that planned each poly multiplication
	if (!(options & POLYMULT_SAVE_PLAN)) free (pmdata->plan), pmdata->plan = NULL;
}

/* Multi-threaded FFT of all the input vectors in a polymult */

void polymult_pre_fft (
	pmhandle *pmdata,		// Handle for polymult library
	gwhandle *gwdata)		// Potentially cloned gwdata
{
	uint64_t j, atomic_val;		// Index of gwnum to work on
	uint64_t atomics_processed = 0;	// Total atomic_vals processed in earlier sections

	// Get the next index to work on
	atomic_val = atomic_fetch_incr (pmdata->helper_counter);

	// FFT invec1 if needed.  FFT the 7 zero padded FFT values in the header.  Even though we are not allowed to change the input vectors, we
	// can write to the 13 values values in the header reserved for use by the polymult library.
	gwnum *invec1 = pmdata->invec1;
	if (! is_preprocessed_poly (invec1)) {
		uint64_t invec1_size = pmdata->invec1_size;
		for  ( ; (j = atomic_val - atomics_processed) < invec1_size; atomic_val = atomic_fetch_incr (pmdata->helper_counter)) {
			if (invec1[j] == NULL) continue;
			gwfft (gwdata, invec1[j], invec1[j]);
			if (gwdata->ZERO_PADDED_FFT) zpad7_fft (gwdata, invec1[j]);
		}
		atomics_processed += invec1_size;
	}

	// Process each of the other polys
	for (int i = 0; i < pmdata->num_other_polys; i++) {
		gwnum *invec2 = pmdata->other_polys[i].invec2;
		// FFT invec2 if needed.  FFT the 7 zero padded FFT values in the header.  Even though we are not allowed to change the input vectors, we
		// can write to the 13 values values in the header reserved for use by the polymult library.
		if (! is_preprocessed_poly (invec2)) {
			uint64_t invec2_size = pmdata->other_polys[i].invec2_size;
			for  ( ; (j = atomic_val - atomics_processed) < invec2_size; atomic_val = atomic_fetch_incr (pmdata->helper_counter)) {
				if (invec2[j] == NULL) continue;
				gwfft (gwdata, invec2[j], invec2[j]);
				if (gwdata->ZERO_PADDED_FFT) zpad7_fft (gwdata, invec2[j]);
			}
			atomics_processed += invec2_size;
		}
		// Also FFT the optional FMA vector
		gwnum *fmavec = pmdata->other_polys[i].fmavec;
		if (fmavec != NULL) {
			uint64_t outvec_size = pmdata->other_polys[i].outvec_size;
			for  ( ; (j = atomic_val - atomics_processed) < outvec_size; atomic_val = atomic_fetch_incr (pmdata->helper_counter)) {
				if (fmavec[j] == NULL) continue;
				gwfft (gwdata, fmavec[j], fmavec[j]);
				if (gwdata->ZERO_PADDED_FFT) zpad7_fft (gwdata, fmavec[j]);
			}
			atomics_processed += outvec_size;
		}
	}
}

/* Multi-threaded unFFT of all the output vectors in a polymult */

void polymult_post_unfft (
	pmhandle *pmdata,		// Handle for polymult library
	gwhandle *gwdata)		// Potentially cloned gwdata
{
	uint64_t j, atomic_val;			// Index of gwnum to work on
	uint64_t atomics_processed = 0;		// Total atomic_vals processed in earlier sections
	polymult_plan *plan = pmdata->plan;	// Plan for implementing the multiplication of invec1 and several invec2s

	// Get the next index to work on
	atomic_val = atomic_fetch_incr (pmdata->helper_counter);

	// Process all of the output polys.  UnFFT results and add in the needed 1*1 values.
	for (int i = 0; i < pmdata->num_other_polys; i++) {
		gwnum	*outvec = pmdata->other_polys[i].outvec;
		uint64_t outvec_size = pmdata->other_polys[i].outvec_size;
		int	options = pmdata->other_polys[i].options + pmdata->options;
		int	addin_value = 1;

		// If POLYMULT_FNMADD is set we will be subtracting the 1*1 value
		if (options & POLYMULT_FNMADD) addin_value = -1;

		// Pre-planning figured out which output entries (if any) need to get 1.0 added to the result because a monic poly is multiplied by a monic poly.
		// We used to add FFT(1) in monic_adjustments but this led to excessive round off error as FFT(1) is NOT a random signal.  It only
		// affects the least significant bits and is apt to get drowned out by the addition of many multiplied large signals.

		// Unfft all output coefficients
		for  ( ; (j = atomic_val - atomics_processed) < outvec_size; atomic_val = atomic_fetch_incr (pmdata->helper_counter)) {
			if (outvec[j] == NULL) continue;
			if (gwdata->ZERO_PADDED_FFT) zpad7_unfft (gwdata, outvec[j]);

			// Set the FFT state so that we know how to unfft the value.  Set the number of unnormalized adds very high so that we know this
			// gwnum was the result of a polymult and unusable without first doing an unfft.  The top coefficient of a monic * monic polymult
			// is a simple add -- use gwadd3o's algorithm for tracking unnormalized adds and determining when a normalization is required.
			// Also, a monic * non-monic top coefficient is a simple 1 * top non-monic coefficient.
			// NOTE: We only support this feature for brute force and Karatsuba polymult.  For FFT polymults the least significant bits of the
			// top coefficient have been degraded by the rounding done in the forward and inverse FFT.  We could fix this by special casing the
			// top coefficient during the polymult FFT, however the savings will be small.
			FFT_state (outvec[j]) = (!gwdata->GENERAL_MMGW_MOD && gwdata->k == 1.0 ? FULLY_FFTed : FFTed_FOR_FMA);
			if (j == outvec_size - 1 &&							// Top outvec coefficient
			    plan->planpart[i].impl != POLYMULT_IMPL_FFT &&				// Not doing a polymult FFT (see comments above)
			    ! (options & POLYMULT_UNFFT_TOP) &&						// Caller not forcing the unfft on top coefficient
			    ((options & POLYMULT_INVEC1_MONIC && options & POLYMULT_INVEC2_MONIC &&	// Monic * monic
			      ((plan->planpart[i].circular_size == plan->planpart[i].true_outvec_size &&// Not doing a circular multiply
			        plan->planpart[i].MSWs_skipped == 1) ||					// Only the 1*1 coefficient is skipped
			       (plan->planpart[i].circular_size == plan->planpart[i].true_outvec_size - 1 &&// Doing a circular multiply
			        plan->planpart[i].MSWs_skipped == 0))) ||				// where only the 1*1 coefficient is skipped
			     (!(options & POLYMULT_INVEC1_MONIC) != !(options & POLYMULT_INVEC2_MONIC) && // Monic * non-monic (or vice-versa)
			      plan->planpart[i].circular_size == plan->planpart[i].true_outvec_size &&	// Not doing a circular multiply
			      plan->planpart[i].MSWs_skipped == 0))) {					// Top coefficient of monic * non-monic is output
				float	unnorms1 = plan->unnorms1;		// Unnormalized add count of top invec1 coefficient
				float	unnorms2 = plan->planpart[i].unnorms2;	// Unnormalized add count of top invec2 coefficient
				// This optimization requires gwset_polymult_safety_margin was called with the output of polymult_safety_margin.
				ASSERTG (gwdata->polymult_safety_margin > 0.0);
				if (! (options & POLYMULT_INVEC2_MONIC))			// Invec1 monic, returning 1 * invec2's top
					unnorms (outvec[j]) = unnorms2;
				else if (! (options & POLYMULT_INVEC1_MONIC))			// Invec2 monic, returning 1 * invec1's top
					unnorms (outvec[j]) = unnorms1;
				else								// Both invecs monic, two tops added together
					unnorms (outvec[j]) = unnorms1 + unnorms2 + 1.0f;
			} else
				unnorms (outvec[j]) = 999.0e9f;					// Huge value to force an unfft

			// See if this value needs to be unffted now or if we are waiting until later
			bool j_affected_by_addin = (j == plan->planpart[i].addin[0] || j == plan->planpart[i].addin[1] ||
						    j == plan->planpart[i].addin[2] || j == plan->planpart[i].addin[3] || j == plan->planpart[i].subout);
			if (!j_affected_by_addin) {
				if (options & POLYMULT_NO_UNFFT) continue;
				if (!polymult_must_unfft (gwdata, outvec[j])) continue;
			}

			// Do the unfft as well as any addin adjustments
//GW: use gwsetaddin rather than gwaddsmall?  We'd need to save/restore the existing addin.  Only affect RLP * RLP, don't bother?
//GW: if k!=1 gwaddsmall of one is not cheap.  It does a dbltogw.  We should cache a gwnum containing 1.0.
			gwunfft2 (gwdata, outvec[j], outvec[j],
				  ((options & (POLYMULT_STARTNEXTFFT | POLYMULT_NEXTFFT)) && !j_affected_by_addin) ? GWMUL_STARTNEXTFFT : 0);
			if (j_affected_by_addin) {
				if (j == plan->planpart[i].addin[0]) gwaddsmall (gwdata, outvec[j], addin_value);
				if (j == plan->planpart[i].addin[1]) gwaddsmall (gwdata, outvec[j], addin_value);
				if (j == plan->planpart[i].addin[2]) gwaddsmall (gwdata, outvec[j], addin_value);
				if (j == plan->planpart[i].addin[3]) gwaddsmall (gwdata, outvec[j], addin_value);
				if (j == plan->planpart[i].subout) gwaddsmall (gwdata, outvec[j], -addin_value);
			}

			// Do an optional forward FFT while data is in the CPU caches
			if (options & POLYMULT_NEXTFFT) gwfft (gwdata, outvec[j], outvec[j]);
		}
		atomics_processed += outvec_size;
	}
}

/*------------------------------------------------------------------------------------------------
|	Easy multi-threading using polymult threads.   Example code!!
+------------------------------------------------------------------------------------------------*/

const int HELPER_OPCODE_COPY = 100001;
const int HELPER_OPCODE_FFT = 100002;
const int HELPER_OPCODE_UNFFT = 100003;
const int HELPER_OPCODE_UNFFT_FFT = 100004;

// Define a structure to pass all needed data to polymult_helper_example
struct poly_helper_example_data {
	pmhandle *pmdata;		// Polymult handle -- mainly to access pmdata->poly_mutex if needed
	int	opcode;			// Work type for poly_helper_example
	union {
		struct {		// Arguments to a poly_copy call
			gwnum	*invec;
			gwnum	*outvec;
			uint64_t poly_size;
		} copy_args;
		struct {		// Arguments to a poly_fft_coefficients, poly_unfft_coefficients, poly_unfft_fft_coefficients call
			gwnum	*vec;
			uint64_t poly_size;
		} fft_args;
	};
};
   
// Copy invec to outvec
void poly_copy (pmhandle *pmdata, gwnum *invec, gwnum *outvec, uint64_t poly_size) {
	struct poly_helper_example_data helper_data;
	// Copy arguments to helper_data so that every thread can access them
	helper_data.opcode = HELPER_OPCODE_COPY;			// Work type for polymult_helper_example to execute
	helper_data.pmdata = pmdata;					// Polymult structure (not needed for this simple example)
	helper_data.copy_args.invec = invec;				// Poly to copy
	helper_data.copy_args.outvec = outvec;				// Destination
	helper_data.copy_args.poly_size = poly_size;			// Size of the poly
	// Multi-threaded poly copy
	pmdata->helper_callback = &poly_helper_example;
	pmdata->helper_callback_data = &helper_data;
	polymult_launch_helpers (pmdata);				// Launch helper threads
}

// UNFFT and/or FFT all the coefficients in a poly.  Why might one want to do this?  In P-1/ECM Stage 2, we start with a large number of size 1 polys.  We multiply
// pairs to create size 2 polys.  We multiply pairs again to create size 4 polys, and so on.  Say you are working with small numbers where the gwnum library
// cannot multi-thread (or poorly multi-threads) gwunfft or gwfft calls.  Say your machine supports 16 threads and polymult is asked to multiply two size 2 polys.
// If polymult, fires up helper threads to gwfft inputs or gwunfft outputs, there are only 4 coefficients to work on leaving 12 threads sitting idle.  The solution,
// is to combine the large number size 2 polys into one gigantic poly and use the routines below to gwunfft and/or gwfft all the coefficients in one batch,
// keeping all 16 threads busy at once.  This is done in conjunction with the POLYMULT_NO_UNFFT polymult option.  Note that using poly_unfft_fft_coefficients
// in the P-1/ECM scenario is more efficient because the gwunfft followed immediately by gwfft is likely to find the gwnum still in the CPU caches.
void poly_fft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size) {
	struct poly_helper_example_data helper_data;
	// Copy arguments to helper_data so that every thread can access them
	helper_data.opcode = HELPER_OPCODE_FFT;				// Work type for polymult_helper_example to execute
	helper_data.pmdata = pmdata;					// Polymult structure (not needed for this simple example)
	helper_data.fft_args.vec = vec;					// Poly to fft coefficients
	helper_data.fft_args.poly_size = poly_size;			// Size of the poly
	// Multi-threaded poly fft coefficients
	pmdata->helper_callback = &poly_helper_example;
	pmdata->helper_callback_data = &helper_data;
	polymult_launch_helpers (pmdata);				// Launch helper threads
}
void poly_unfft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size) {
	struct poly_helper_example_data helper_data;
	// Copy arguments to helper_data so that every thread can access them
	helper_data.opcode = HELPER_OPCODE_UNFFT;			// Work type for polymult_helper_example to execute
	helper_data.pmdata = pmdata;					// Polymult structure (not needed for this simple example)
	helper_data.fft_args.vec = vec;					// Poly to unfft coefficients
	helper_data.fft_args.poly_size = poly_size;			// Size of the poly
	// Multi-threaded poly unfft coefficients
	pmdata->helper_callback = &poly_helper_example;
	pmdata->helper_callback_data = &helper_data;
	polymult_launch_helpers (pmdata);				// Launch helper threads
}
void poly_unfft_fft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size) {
	struct poly_helper_example_data helper_data;
	// Copy arguments to helper_data so that every thread can access them
	helper_data.opcode = HELPER_OPCODE_UNFFT_FFT;			// Work type for polymult_helper_example to execute
	helper_data.pmdata = pmdata;					// Polymult structure (not needed for this simple example)
	helper_data.fft_args.vec = vec;					// Poly to unfft/fft coefficients
	helper_data.fft_args.poly_size = poly_size;			// Size of the poly
	// Multi-threaded poly unfft/fft coefficients
	pmdata->helper_callback = &poly_helper_example;
	pmdata->helper_callback_data = &helper_data;
	polymult_launch_helpers (pmdata);				// Launch helper threads
}


// Example helper routine
void poly_helper_example (
	int	helper_num,		// 0 = main thread, 1+ = helper thread num
	gwhandle *gwdata,		// Single-threaded and *MOSTLY* thread-safe gwdata (probably cloned) to use.  Each helper thread gets its own gwdata.
	void	*info)			// Can point to anything you choose.  In this example it points to struct polymult_helper_example_data
{
	struct poly_helper_example_data *helper_data = (struct poly_helper_example_data *) info;
	pmhandle *pmdata = helper_data->pmdata;

/* Copy a poly.  This example demonstrates using an atomic counter to get next available coefficient to work on. */

	if (helper_data->opcode == HELPER_OPCODE_COPY) {
		// Loop copying coefficients from invec to outvec
		gwnum *invec = helper_data->copy_args.invec;
		gwnum *outvec = helper_data->copy_args.outvec;
		for ( ; ; ) {
			// Get poly coefficient number we are to copy
			uint64_t coeff = atomic_fetch_incr (pmdata->helper_counter);

			// If the other helper threads have completed copying invec to outvec, then we're done
			if (coeff >= helper_data->copy_args.poly_size) break;

			// Copy the gwnum
			gwcopy (gwdata, invec[coeff], outvec[coeff]);
		}
	}

/* FFT a poly's coefficients */

	else if (helper_data->opcode == HELPER_OPCODE_FFT) {
		// Loop FFTing coefficients in vec
		gwnum *vec = helper_data->fft_args.vec;
		for ( ; ; ) {
			// Get poly coefficient number to FFT
			uint64_t coeff = atomic_fetch_incr (pmdata->helper_counter);

			// If the other helper threads have completed FFTing vec, then we're done
			if (coeff >= helper_data->fft_args.poly_size) break;

			// FFT the gwnum
			gwfft (gwdata, vec[coeff], vec[coeff]);
		}
	}

/* UNFFT a poly's coefficients */

	else if (helper_data->opcode == HELPER_OPCODE_UNFFT) {
		// Loop UNFFTing coefficients in vec
		gwnum *vec = helper_data->fft_args.vec;
		for ( ; ; ) {
			// Get poly coefficient number to UNFFT
			uint64_t coeff = atomic_fetch_incr (pmdata->helper_counter);

			// If the other helper threads have completed UNFFTing vec, then we're done
			if (coeff >= helper_data->fft_args.poly_size) break;

			// UNFFT the gwnum if it is the output of a polymult
			if (polymult_must_unfft (gwdata, vec[coeff])) gwunfft (gwdata, vec[coeff], vec[coeff]);
		}
	}

/* UNFFT then FFT a poly's coefficients */

	else if (helper_data->opcode == HELPER_OPCODE_UNFFT_FFT) {
		// Loop UNFFTing coefficients in vec
		gwnum *vec = helper_data->fft_args.vec;
		for ( ; ; ) {
			// Get poly coefficient number to UNFFT/FFT
			uint64_t coeff = atomic_fetch_incr (pmdata->helper_counter);

			// If the other helper threads have completed UNFFT/FFTing vec, then we're done
			if (coeff >= helper_data->fft_args.poly_size) break;

			// UNFFT the gwnum if it is the output of a polymult
			if (polymult_must_unfft (gwdata, vec[coeff])) gwunfft2 (gwdata, vec[coeff], vec[coeff], GWMUL_STARTNEXTFFT);
			gwfft (gwdata, vec[coeff], vec[coeff]);
		}
	}

/* Raise error, caller probably forgot to reset pmdata->helper_callback after a call to one of these four sample routines. */

	else
		ASSERTG(FALSE);
}

#endif
