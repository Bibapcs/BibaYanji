/*----------------------------------------------------------------------
| polymult.h
|
| This file contains the headers and definitions that are used in the
| polynomial multiplication built upon the gwnum library.
| 
|  Copyright 2021-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _POLYMULT_H
#define _POLYMULT_H

#include "gwthread.h"		// Needed for atomics

/* This is a C library.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct pmhandle_struct pmhandle;

// These routines can be used before polymult_init.  Use the information to select a gwnum FFT size with sufficient safety_margin
// to do polymults of the desired size.

// Get the needed safety_margin required for an invec1_size by invec2_size polymult.
// There are 2 recommended ways to setup gwnum for use with polymult.
// 1) Before gwsetup call gwset_polymuly_safety_margin with the output from polymult_safety_margin, or
// 2) If the polynomial vector sizes are not known ahead of time because they depend on the size of a gwnum.  Then loop:
//	for (int larger_fft_size = 0; ; larger_fft_size++) {
//		gwset_larger_fftlen_count (larger_fft_size);
//		gwsetup;
//		calculate polynomial vector sizes based on available memory and gwnum size;
//		if (gw_passes_safety_margin (polymult_safety_margin (calculated_vector_sizes)) break;
//	}
//	gwset_polymult_safety_margin (polymult_safety_margin (calculated_vector_sizes));
// WARNING: If polymult is used to square a polynomial you'll need an extra EB_GWMUL_SAVINGS safety margin.
float polymult_safety_margin (uint64_t invec1_size, uint64_t invec2_size);

// Get the FFT size that will be used for an n = invec1_size + invec2_size polymult
uint64_t polymult_fft_size (uint64_t n);

// Get the memory (in bytes) required for an FFT based polymult.  Use the information to ensure over-allocating memory does not occur.
uint64_t polymult_mem_required (
	pmhandle *pmdata,		// Handle for polymult library
	uint64_t invec1_size,		// Size of poly #1
	uint64_t invec2_size,		// Size of poly #2
	int	options);		// Polymult options

/* Callers of the polymult routines must first allocate a pmhandle (on the heap or stack) and pass it to all polymult routines. */
// Initialize a polymult handle
void polymult_init (
	pmhandle *pmdata,		// Handle for polymult library that needs initializing
	gwhandle *gwdata);		// Handle for gwnum FFT library

// Control how many threads to use during polymult (or using polymult helper threads with a user-defined callback).  The maximum must be set prior to first
// call to polymult.  One can use fewer than the maximum number of threads -- this might be useful in cases where hyperthreading benefits only some work types.
// NOTE: If gwnum library has a thread_callback routine set, the polymult library will use the same callback routine with action code 20 and 21. */
#define polymult_set_max_num_threads(h,n)	(h)->max_num_threads = (h)->num_threads = (n)
#define polymult_set_num_threads(h,n)		(h)->num_threads = (n)

// Set default polymult tuning using CPU cache sizes.  If this routine is not called, default tuning parameters are set using L2 cache of 256KB, and
// L3 cache of 6144KB (6MB).  These tuning defaults are almost certainly imperfect.  Feel free to change the tuning defaults to suit your exact needs.
// Read the polymult_default_tuning code to see some of the considerations used in selecting tuning parameters.  Also, run prime95 Advanced/Time 8900
// for relevant timings on your CPU.  Beware, these timings can vary considerably from run to run.
void polymult_default_tuning (
	pmhandle *pmdata,		// Handle for polymult library
	uint32_t L2_CACHE_SIZE,		// Optimize FFTs to fit in this size cache (number is in KB).  Default is 256KB.
	uint32_t L3_CACHE_SIZE);	// Optimize FFTs to fit in this size cache (number is in KB).  Default is 6144KB (6MB).

// Terminate use of a polymult handle.  Free up memory.
void polymult_done (
	pmhandle *pmdata);		// Handle for polymult library

/* Multiply two polynomials.  It is safe to use input gwnums in the output vector.  For a normal polynomial multiply outvec_size must be */
/* invec1_size + invec2_size - 1.  For monic polynomial multiply, the leading input coefficients of 1 are omitted as is the leading 1 output */
/* coefficient -- thus outvec_size must be invec1_size + invec2_size. */
/* Entries in outvec can be NULL if caller is not interested in those output coefficients. */
void polymult (
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// First input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	gwnum	*invec2,		// Second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	gwnum	*outvec,		// Output poly
	uint64_t outvec_size,		// Size of the output polynomial or if POLYMULT_CIRCULAR is set compute result modulo (X^outvec_size - 1)
					// or if POLYMULT_MULHI or POLYMULT_MULLO is set this is the number of coefficients to return.  This interface
					// does not allow the POLYMULT_CIRCULAR option and POLYMULT_MULHI or POLYMULT_MULLO to both be set -- use
					// polymult2 or polymult_several instead.
	int	options);

/* Multiply two polynomials with a fused multiply add. */
void polymult_fma (
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// First input poly
	uint64_t invec1_size,		// Size of the first input polynomial
	gwnum	*invec2,		// Second input poly
	uint64_t invec2_size,		// Size of the second input polynomial
	gwnum	*outvec,		// Output poly
	uint64_t outvec_size,		// Size of the output polynomial or if POLYMULT_CIRCULAR is set compute result modulo (X^outvec_size - 1)
					// or if POLYMULT_MULHI or POLYMULT_MULLO is set this is the number of coefficients to return.  This interface
					// does not allow the POLYMULT_CIRCULAR option and POLYMULT_MULHI or POLYMULT_MULLO options to both be set -- use
					// polymult_several instead.
	gwnum	*fmavec,		// FMA poly to add or subtract from poly multiplication result.  Same size as outvec, cannot be preprocessed.
	int	options);

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
	int	options);

#define POLYMULT_INVEC1_MONIC	0x1	// Invec1 is a monic polynomial.  Leading coefficient of one is implied.
#define POLYMULT_INVEC2_MONIC	0x2	// Invec2 is a monic polynomial.  Leading coefficient of one is implied.
#define POLYMULT_INVEC1_RLP	0x4	// Invec1 is an RLP (Reciprocal Laurent Polynomial).  Needs only half the storage.
#define POLYMULT_INVEC2_RLP	0x8	// Invec2 is an RLP (Reciprocal Laurent Polynomial).  Needs only half the storage.
#define POLYMULT_INVEC1_NEGATE	0x10	// Invec1 coefficients are negated.  The implied one of a monic polynomial is not negated.
#define POLYMULT_INVEC2_NEGATE	0x20	// Invec2 coefficients are negated.  The implied one of a monic polynomial is not negated.
#define POLYMULT_INVEC1_MONIC_RLP	(POLYMULT_INVEC1_MONIC | POLYMULT_INVEC1_RLP)		// A shorthand
#define POLYMULT_INVEC2_MONIC_RLP	(POLYMULT_INVEC2_MONIC | POLYMULT_INVEC2_RLP)		// A shorthand
#define POLYMULT_INVEC1_MONIC_NEGATE	(POLYMULT_INVEC1_MONIC | POLYMULT_INVEC1_NEGATE)	// A shorthand
#define POLYMULT_INVEC2_MONIC_NEGATE	(POLYMULT_INVEC2_MONIC | POLYMULT_INVEC2_NEGATE)	// A shorthand
#define POLYMULT_CIRCULAR	0x100	// Circular convolution based on outvec_size.  Multiplication result is modulo (X^outvec_size - 1).  If using the
					// polymult_several interface, the result is modulo (X^circular_size - 1).
#define POLYMULT_MULHI		0x200	// Return only the outvec_size higher degree coefficients.  Using both POLYMULT_CIRCULAR
					// and POLYMULT_MULHI is only allowed in the polymult_several interface.
#define POLYMULT_MULLO		0x400	// Return only the outvec_size lower degree coefficients.  Using both POLYMULT_CIRCULAR
					// and POLYMULT_MULLO is only allowed in the polymult_several interface.
#define POLYMULT_MULMID		0x800	// Return outvec_size coefficients from middle of the polymult result.  Only allowed in the polymult_several interface.
#define POLYMULT_NO_UNFFT	0x1000	// Do not perform the required unfft on output coefficients.  Caller might use this option to multithread these gwunfft
					// calls or do additional work after the gwunfft call while the gwnum is in the CPU caches.
#define POLYMULT_STARTNEXTFFT	0x2000	// Similar to GWMUL_STARTNEXTFFT.  Applied to all output coefficients.
#define	POLYMULT_NEXTFFT	0x4000	// Perform both the required unfft and a forward FFT on output coefficients.  Caller might use this option because
					// it may perform better because the gwnum is in the CPU caches.
#define POLYMULT_FMADD		0x8000	// Compute invec1 * invec2 + fmavec
#define POLYMULT_FMSUB		0x10000	// Compute invec1 * invec2 - fmavec
#define POLYMULT_FNMADD		0x20000	// Compute fmavec - invec1 * invec2
#define POLYMULT_UNFFT_TOP	0x40000	// When multiplying polynomials with a monic input the resulting top coefficient does not involve any multiplications.
					// It should be safe to use this coefficient without a gwunfft operation.  The default behavior is to not call gwunfft
					// on this coefficient.  This option forces the unfft of the top coefficient.
#define POLYMULT_SAVE_PLAN	0x80000	// Save pmdata->plan used for this polymult call.  Planning can be a significant overhead multiplying tiny polys.
#define POLYMULT_USE_PLAN	0x100000 // Use previously generated pmdata->plan

// The following options only apply to polymult_preprocess
#define	POLYMULT_PRE_FFT	0x40	// Compute the forward FFT while creating a preprocessed polynomial
#define	POLYMULT_PRE_COMPRESS	0x80	// Compress each double while creating a preprocessed polynomial

/*-----------------------------------------------------------------------------
|	Advanced polymult routines
+----------------------------------------------------------------------------*/

// Accessing the preprocessed header directly is frowned upon, several accessor macros follow -- probably shouldn't use any but the first three
#define is_preprocessed_poly(p)		(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->self_ptr == (p))
#define is_preffted_poly(p)		(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->options & POLYMULT_PRE_FFT)
#define is_compressed_poly(p)		(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->options & POLYMULT_PRE_COMPRESS)
#define preprocessed_fft_size(p)	(intptr_t) (((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->line_size)
#define preprocessed_num_elements(p)	(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->num_compressed_blks)
#define preprocessed_element_size(p)	(intptr_t) (((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->element_size)
#define preprocessed_poly_size(p)	(preprocessed_num_elements(p) * preprocessed_element_size(p))
#define preprocessed_monics_included(p)	(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->monic_ones_included)
#define preprocessed_top_unnorms(p)	(((preprocessed_poly_header *)((char *)(p)-sizeof(gwarray_header)))->top_unnorms)

/* Preprocess a poly that will be used in multiple future polymult calls.  Preprocessing can reduce memory consumption or reduce CPU time. */
/* Returns a massaged poly.  Caller should then free the unmassaged poly.  The massaged poly cannot be used in any gwnum calls, it can only be used */
/* in future polymult calls with poly sizes and options that match those passed to this routine. */
/* The POLYMULT_PRE_FFT preprocessing option allows the forward FFT of invec1 to be used over and over again.  The downside to the POLYMULT_PRE_FFT option */
/* is the preprocessed poly consumes more memory. */
gwarray polymult_preprocess (		// Returns a plug-in replacement for the input poly
	pmhandle *pmdata,		// Handle for polymult library
	gwnum	*invec1,		// Input poly
	uint64_t invec1_size,		// Size of the input polynomial
	uint64_t invec2_size,		// Size of the other polynomial that will be used in a future polymult call
	uint64_t outvec_size,		// Size of the output polynomial that will be used in a future polymult call
	int	options);		// Future polymult call options plus preprocessing options (FFT, compress -- see below)

/* This routine allows multiplying one poly with several other polys.  This yields a small performance gain in that the one poly is read and FFTed just once. */

typedef struct pmargument_struct {	// Each of the several polys need to describe where their input and output coefficients are located
	gwnum	*invec2;	// Second input poly
	uint64_t invec2_size;	// Size of the second input polynomial
	gwnum	*outvec;	// Output poly
	uint64_t outvec_size;	// Size of the output polynomial
	gwnum	*fmavec;	// Poly to add in if FMA options are requested
	uint64_t circular_size;	// If POLYMULT_CIRCULAR set, compute poly result modulo (X^circular_size - 1)
	uint64_t first_mulmid;	// If POLYMULT_MULMID set, this is the number of least significant coefficients that will not be returned
	int	options;	// Any of the polymult options that are not related to poly #1
} polymult_arg;

void polymult_several (		// Multiply one poly with several polys
	pmhandle *pmdata,	// Handle for polymult library
	gwnum	*invec1,	// First input poly
	uint64_t invec1_size,	// Size of the first input polynomial
	polymult_arg *other_polys, // Array of "other poly descriptors" to multiply with first input poly (describes each second input poly and output poly)
	int	num_other_polys,// Number of other polys to multiply with first input poly
	int	options);	// Poly #1 options.  Options not associated with poly #1 are applied to all other polys.

// Obscure macro to test if a polymult output coefficient must be unffted.  When using POLYMULT_NO_UNFFT this macro detects output coefficients that do not
// require an unfft because of the optimization regarding top coefficients in monic polymults.

//GW: Relax EB check (see gwadd3o) to numadds_to_eb(c) + 1 or numadds_to_eb(c)????
#define polymult_must_unfft(h,c) (FFT_state(c)==FFTed_FOR_FMA || (FFT_state(c)==FULLY_FFTed && EB_GWMUL_SAVINGS + numadds_to_eb(unnorms(c))*2.0f > (h)->EXTRA_BITS))

/*------------------------------------------------------------------------------------------------
|	Easy multithreading using polymult threads.   Complete with example code!!
|	Study poly_helper_example in polymult.c so you can write your own helper function.
+------------------------------------------------------------------------------------------------*/

/* Underlying routine that allows users of this library to use the polymult helper threads and locking mechanisms to craft their own multithreaded */
/* helper routines.  The helper_callback and helper_callback_data fields must be set prior to launching the helpers.  Each helper is given its own clone */
/* of gwdata to safely call gwnum operations -- cloning is necessary as independent threads cannot use use same gwdata structure. */

/* This routine launches the polymult helper threads.  Has the calling thread also help, then waits for the calling thread and all helper threads to finish. */
/* The polymult library uses this routine to do multithreading, you can too! */
void polymult_launch_helpers (
	pmhandle *pmdata);		// Handle for polymult library

// Example using polymult threads for faster execution of some simple poly utilities.  Study the code in polymult.c.
void poly_helper_example (int helper_num, gwhandle *gwdata, void *info);

/* Poly_helper_example provides a multithreaded implementation of the following utility routines */

// Copy invec to outvec
void poly_copy (pmhandle *pmdata, gwnum *invec, gwnum *outvec, uint64_t poly_size);

// UNFFT and/or FFT all the coefficients in a poly.  Why might one want to do this?  In P-1/ECM Stage 2, we start with a large number of size 1 polys.  We multiply
// pairs to create size 2 polys.  We multiply pairs again to create size 4 polys, and so on.  Say you are working with small numbers where the gwnum library
// cannot multithread (or poorly multithreads) gwunfft or gwfft calls.  Say your machine supports 16 threads and polymult is asked to multiply two size 2 polys.
// If polymult, fires up helper threads to gwfft inputs or gwunfft outputs, there are only 4 coefficients to work on leaving 12 threads sitting idle.  The solution,
// is to combine the large number of size 2 polys into one gigantic poly and use the routines below to gwunfft and/or gwfft all the coefficients in one batch,
// keeping all 16 threads busy at once.  This is done in conjunction with the POLYMULT_NO_UNFFT polymult option.  Note that using poly_unfft_fft_coefficients
// in the P-1/ECM scenario is more efficient because gwunfft followed immediately by gwfft is likely to find the gwnum still in the CPU caches.
void poly_fft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size);
void poly_unfft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size);
void poly_unfft_fft_coefficients (pmhandle *pmdata, gwnum *vec, uint64_t poly_size);

	
/*-----------------------------------------------------------------------------
|	Internal polymult library data structures
+----------------------------------------------------------------------------*/

/* The pmhandle structure containing all of polymult's "global" data. */
struct pmhandle_struct {
	gwhandle *gwdata;		// Handle for gwnum FFT library
	int	max_num_threads;	// Maximum number of threads that can be used to compute polymults
	int	num_threads;		// Number of threads to use computing polymults (must not exceed max_num_threads)
	int	num_lines;		// During polymult each gwnum is split into "lines".  Each thread works on one line at at a time.
	gwevent work_to_do;		// Event (if not spin waiting) to signal polymult helper threads there is work to do
	gwatomic alt_work_to_do;	// Atomic alternative to work to do mutex when spin waiting
	gwevent	all_helpers_done;	// Event (if not spin waiting) to signal main thread that the auxiliary threads are done
	gwatomic num_active_helpers;	// Number of active helpers (awakened from the work_to_do event).  Is also the alternative to all_helpers_done mutex.
	gwmutex	poly_mutex;		// Mutex to make polymult thread safe when multi-threading
	gwatomic next_thread_num;	// Lets us generate a unique id for each helper thread
	bool volatile all_work_assigned; // Flag indicating all helper thread work has been assigned (some helpers ma still be active)
	gwthread *thread_ids;		// Thread ids for the spawned threads
	uint64_t twiddles_initialized;	// FFT size twiddle tables are built for
	bool	twiddles_are_from_cache; // TRUE if the current twiddles came from the twiddle cache
	double	*twiddles1;		// Sin/cos table for radix-3
	double	*twiddles2;		// Sin/cos table for radix-4 and radix-5
	// Brute force from outvec_size [1..KARAT_BREAK), Karatsuba from [KARAT_BREAK..FFT_BREAK), FFT from [FFT_BREAK..infinity)
	int	KARAT_BREAK;		// Output vector size where we switch from brute force to Karatsuba
	int	FFT_BREAK;		// Output vector size where we switch from Karatsuba to FFTs
	// Tuning parameters, assigned by polymult_default_tuning.  These defaults can be safely overridden.
	uint32_t two_pass_start;	// FFT size at which we switch from one pass FFTs to two pass FFTs.
	uint32_t max_pass2_size;	// Maximum size for pass 2 in two pass FFTs.
	uint64_t mt_ffts_start;		// FFT size at which we switch from multi-thread lines to multi-threading the FFT
	uint64_t mt_ffts_end;		// FFT size at which we switch back to multi-thread lines from multi-threading the FFT
	uint64_t streamed_stores_start;	// FFT size at which streaming stores are used.  Streamed stores may improve performance when FFT lines don't fit in the caches.
	uint64_t strided_writes_end;	// FFT size at which strided writes are no longer used. Strided writes may improves performance on some CPUs in some cases.
	// Cached twiddles
	bool	cached_twiddles_enabled;// TRUE if caching twiddles is enabled
	bool	twiddle_cache_additions_disabled; // TRUE if adding new entries to the twiddle cache is temporarily disabled
	struct {
		uint64_t size;		// FFT size the twiddles were build for
		double	*twiddles1;	// Sin/cos table for radix-3
		double	*twiddles2;	// Sin/cos table for radix-4 and radix-5
	} cached_twiddles[40];
	int	cached_twiddles_count;	// Number of cached twiddles
	// Arguments to the current polymult call.  Copied here so that helper threads can access the arguments.  Also, the plan for implementing the polymult.
	gwnum	*invec1;		// First input poly
	uint64_t invec1_size;		// Size of the first input polynomial
	polymult_arg *other_polys;	// Array of second polys
	int	num_other_polys;	// Number of second polys
	int	options;
	bool	mt_polymult_line;	// TRUE if polymult_line is multi-threaded and read_line, fft_line_pass, write_line is single-threaded
	void	*plan;			// Plan for implementing the multiplication of invec1 and several invec2s
	void	(*internal_callback)(int, pmhandle *, void *); // Internal routine to multithread read_line, fft_line, write_line 
	void	*internal_callback_data; // Data needed to multithread read_line, fft_line, write_line
	// These items allow users of the polymult library to also use the polymult helper threads for whatever they see fit.
	void	(*helper_callback)(int, gwhandle *, void *); // User-defined helper callback routine
	void	*helper_callback_data;	// User-defined data to pass to the user-defined helper callback routine
	int volatile helper_opcode;	// Opcode if helpers are doing polymult work, zero if doing user work
	gwatomic helper_counter;	// Used internally to increment through helper work.  User-defined helpers can use this too.
	int	saved_gwdata_num_threads; // Used internally to restore gwdata's multithreading after a user defined callback
	gwhandle *stats_gwdata;		// The cloned gwdata that is accumulating stats as each user-defined helper finshes up
};

// Header for a preprocessed poly.  Conceptually a preprocessed poly is an array of invec1 or invec2 as returned by read_line called from polymult_line.
// Rather than pulling one cache line out of each gwnum, the cache lines are in contiguous memory (this reduces mem consumption by ~1.5% as it eliminates
// 64 pad bytes every 4KB in most gwnums.  Contiguous memory ought to be quicker to access too.  Each invec1 or invec2 can optionally be FFTed (likely
// increases memory consumption due to zero padding) to reduce CPU time in each future polymult call.  Each invec can also be compressed reducing memory
// consumption by ~12.5% -- we can compress ~8 bits out of each 11-bit exponent in a double.
typedef struct {
	gwarray_header linkage;		// Used to put preprocessed polys on gwarray linked list.  Allows gwdone to free all preprocessed polys.
	gwnum	*self_ptr;		// Ptr to itself.  A unique indicator that this is a preprocessed poly.
	uint64_t element_size;		// Size in bytes of each line in the preprocessed array
	uint64_t line_size;		// Size in CVDTs of each line in the preprocessed array.  If POLYMULT_PRE_FFT is set this is the poly FFT size.
	int	num_compressed_blks;	// When compressing, each line is sub-divided into this many blocks (allows multi-threading decompression)
	int	options;		// Copy of options passed to polymult_line_preprocess
	float	top_unnorms;		// Number of unnormalized adds for the topmost poly coefficient
	bool	monic_ones_included;	// TRUE if monic ones are included in pre-FFTed data
} preprocessed_poly_header;

#ifdef __cplusplus
}
#endif

#endif
