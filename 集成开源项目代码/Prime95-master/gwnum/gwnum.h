/*----------------------------------------------------------------------
| gwnum.h
|
| This file contains the headers and definitions that are used in the
| multi-precision IBDWT arithmetic routines.  That is, all routines
| that deal with the gwnum data type.
|
| Gwnums are great for applications that do a lot of multiplies modulo
| a number.  Only Intel x86-platforms are supported.  Add and subtract
| are also pretty fast.
|
| Gwnums are not suited to applications that need to convert to and from
| binary frequently or need to change the modulus frequently.
|
| MULTI-THREAD WARNING: You CAN perform gwnum operations in different
| threads IF AND ONLY IF each uses a different gwhandle structure
| initialized by gwinit.
| 
|  Copyright 2002-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWNUM_H
#define _GWNUM_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Include common definitions */

#include "gwcommon.h"
#include "giants.h"
#include "gwthread.h"

/* To support multithreading, callers of the gwnum routines must allocate a gwhandle (on the heap or stack) and pass it to */
/* all gwnum routines.  gwinit and gwsetup fill this structure up with lots of data that used to be stored in global variables. */
typedef struct gwhandle_struct gwhandle;

/* The gwnum data type.  A gwnum points to an array of doubles - the FFT data.  In practice, there is */
/* data stored before the doubles.  See the internals section below if you really must know. */
typedef double *gwnum;

/* An array of gwnums data type.  In practice, there is data stored before the array. */
typedef gwnum *gwarray;

/*---------------------------------------------------------------------+
|                     SETUP AND TERMINATION ROUTINES                   |
+---------------------------------------------------------------------*/

/* This is the version number for the gwnum libraries. It changes whenever there is a significant change to the gwnum code. */
/* Since Prime95 also uses the same version numbering scheme, you will see some strange jumps in gwnum version numbers when there */
/* are new prime95 versions without any changes in the gwnum code.  This version number is also embedded in the assembly code and */
/* gwsetup verifies that the version numbers match.  This prevents bugs from accidentally linking in the wrong gwnum library. */

#define GWNUM_VERSION		"30.19"
#define GWNUM_MAJOR_VERSION	30
#define GWNUM_MINOR_VERSION	19

/* Error codes returned by the three gwsetup routines */

#define GWERROR_VERSION		1001	/* GWNUM.H and FFT assembly code version numbers do not match. */
#define GWERROR_TOO_LARGE	1002	/* Number too large for the FFTs */
#define GWERROR_K_TOO_SMALL	1003	/* k < 1 is not supported */
#define GWERROR_K_TOO_LARGE	1004	/* k > 53 bits is not supported */
#define GWERROR_MALLOC		1005	/* Insufficient memory available */
#define GWERROR_VERSION_MISMATCH 1006	/* GWNUM_VERSION from gwinit call doesn't match GWNUM_VERSION when gwnum.c was compiled. */
#define GWERROR_STRUCT_SIZE_MISMATCH 1007 /* Gwhandle structure size from gwinit call doesn't match size */
					/* when gwnum.c was compiled.  Check compiler alignment switches. */
#define GWERROR_TOO_SMALL	1008	/* Gwsetup called on a number <= 1 */
#define GWERROR_NO_INIT		1009	/* gwinit was not called prior to gwsetup */
#define GWERROR_ZERO_THREADS	1010	/* num_threads set to zero */
#define GWERROR_INTERNAL	2000	/* 2000 and up are "impossible" internal errors. */

/* Error codes returned by gwtobinary, gwtogiant, and get_fft_value */

#define GWERROR_BAD_FFT_DATA	-1	/* Nan or inf data encountered */
#define GWERROR_PARTIAL_FFT	-1009	/* Attempt to convert a partially FFTed number to binary */
#define GWERROR_FFT		-1010	/* Attempt to convert an FFTed number to binary */
#define GWERROR_FFT_FOR_FMA	-1011	/* Attempt to convert an FFTed-for-FMA number to binary */

/* Prior to calling gwsetup, you MUST CALL gwinit.  This initializes the gwhandle structure. */
/* It gives us a place to set rarely used gwsetup options prior to calling gwsetup. */
#define gwinit(h)		gwinit2 (h, sizeof (gwhandle), GWNUM_VERSION)
/* The gwinit function has been superceeded by gwinit2.  By passing in the version number we can verify the caller */
/* used the same gwnum.h file as the one he eventually links with.  The sizeof (gwhandle) structure is used to verify */
/* he compiles with the same structure alignment options that were used when compiling gwnum.c.  For compatibility with */
/* existing code we delay reporting any compatibility problems until gwsetup is called. */
void gwinit2 (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	int	struct_size,	/* Size of the gwdata structure */
	const char *version_string);

/* There are three different setup routines.  The first, gwsetup, is for gwnum's primary use - support for fast */
/* operations modulo K*B^N+C.  Smaller K and C values result in smaller FFT sizes and faster operations. */
int gwsetup (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	double	k,		/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,	/* B in K*B^N+C. */
	unsigned long n,	/* N in K*B^N+C. Exponent to test. */
	signed long c);		/* C in K*B^N+C. Must be rel. prime to K. */

/* This setup routine is for operations modulo an arbitrary binary number.  This is three times slower than the special */
/* forms above.  The code will try to convert suitable k*2^n+c values into the faster gwsetup (gwdata,k,b,n,c) call above. */
/* The caller would be better off not relying on this detection if at all possible. */
int gwsetup_general_mod (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	const uint32_t *array,	/* The modulus as an array of 32-bit values */
	uint32_t arraylen);	/* Number of values in the array */
int gwsetup_general_mod_64 (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	const uint64_t *array,	/* The modulus as an array of 64-bit values */
	uint64_t arraylen);	/* Number of values in the array */

/* This setup routine is for operations without a modulo. In essence, */
/* you are using gwnums as a general-purpose FFT multiply library. */
int gwsetup_without_mod (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	unsigned long n);	/* Maximum number of bits in OUTPUT numbers. */

/* Free all memory allocated by gwnum routines since gwsetup was called. */
void gwdone (
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/*---------------------------------------------------------------------+
|                    GWNUM OBSCURE GWSETUP OPTIONS                     |
+---------------------------------------------------------------------*/

/* Prior to calling one of the gwsetup routines, you can tell the library how many compute threads it can use to perform a multiply. */
#define gwset_num_threads(h,n)		((h)->num_threads = n)
#define gwget_num_threads(h)		((h)->num_threads)

/* Specify a call back routine for the auxiliary threads to call when they are created.  This lets the user of the gwnum library */
/* set the thread priority and affinity as it sees fit.  You can also specify an arbitrary pointer to pass to the callback routine. */
/* The callback routine must be declared as follows: */
/*	void callback (int thread_num, int action, void *data) */
/* If you tell gwnum to use 4 threads, it will create 3 auxiliary threads and invoke the callback routine with thread_num = 1, 2, and 3. */
/* Action is 0 for thread starting and 1 for thread terminating.  Action is 10 for prefetching hyperthread starting and 11 for */
/* prefetching hyperthread terminating (see gwset_hyperthread_prefetch). */
#define gwset_thread_callback(h,n)		((h)->thread_callback = n)
#define gwset_thread_callback_data(h,d)		((h)->thread_callback_data = d)

/* Prior to calling one of the gwsetup routines, you can have the library play it safe" by reducing the maximum allowable bits */
/* per FFT data word.  For example, the code normally tests a maximum of 22477 bits in a 1024 SSE2 FFT, or 21.95 bits per double. */
/* If you set the safety margin to 0.5 then the code will only allow 21.45 bits per double, or a maximum of 21965 bits in a 1024 length FFT. */
/* You can also use this option to "live dangerously" by increasing the maximum allowable bits per FFT data word - just set the */
/* safety margin to a negative value. */
#define gwset_safety_margin(h,m)	((h)->safety_margin = (float) (m))

/* The gwsetup routines need to know the maximum value that will be used in a call to gwsetmulbyconst.  By default this value is */
/* assumed to be 3, which is what you would use in a base-3 Fermat PRP test.  Gwsetup must switch to a generic modular reduction */
/* if k * mulbyconst or c * mulbyconst is too large.  Call this routine prior to calling gwsetup. */
#define gwset_maxmulbyconst(h,c)	((h)->maxmulbyconst = c)
#define gwsetmaxmulbyconst		gwset_maxmulbyconst

/* When doing a gwsetup_general_mod, the library prefers to use an integral number of bits per word (a rational FFT) because they are */
/* a little faster than irrational FFTs.  However, some moduli create non-random data when using rational FFTs.  For example, if we test */
/* (10^828809-1)/9 and put exactly 18 bits into each FFT word, then every FFT word in GW_MODULUS_FFT will contains the same value! */
/* Not exactly the random data the FFTs require for small roundoff errors.  This routine takes a boolean to force use of the safer irrational FFTs. */
#define gwset_irrational_general_mod(h,b)  ((h)->use_irrational_general_mod = (char) (b))

/* Prior to calling one of the gwsetup routines, you can force the library to use a larger fft length than normal. */
/* The input argument specifies how many FFT sizes larger than normal you would like.  You might use this */
/* routine if you are having roundoff errors using the normal FFT length. */
#define gwset_larger_fftlen_count(h,n)	((h)->larger_fftlen_count = n)

/* Select the smallest valid FFT length equal to or greater than the specified FFT length.  You might use this routine to select a larger FFT if you are */
/* having roundoff errors using the normal FFT length (or use gwset_larger_fftlen_count).  To select a smaller FFT length than normal (NOT RECOMMENDED!) you */
/* must use gwset_safety_margin with a negative value.  NOTE: Prior to version 30.12 setting minimum fftlen would select a smaller FFT length than normal. */
#define gwset_minimum_fftlen(h,n)	((h)->minimum_fftlen = n)

/* Set this if FFTs will use hyperthreading. This may affect selection of fastest FFT implementation.  By default, */
/* it is assumed hyperthreading will not be used as for most CPUs hyperthreading is not faster and uses more electricity. */
#define gwset_will_hyperthread(h,n)	((h)->will_hyperthread = n)
#define gwclear_will_hyperthread(h)	((h)->will_hyperthread = 0)

/* Prior to calling one of the gwsetup routines, you can have the library use benchmark data stored in gwnum.txt to select the fastest */
/* FFT implementation.  This is the default behavior. */
#define gwset_use_benchmarks(h)		((h)->use_benchmarks = 1)
#define gwclear_use_benchmarks(h)	((h)->use_benchmarks = 0)

/* Set this if it is known how many cores will be used in total -- either by your program or multiple instances of your program. */
/* By default, this value is the number of cores on the machine, which means the user of your program will keep all cores fully */
/* occupied with gwnum work.  This setting may affect selection of fastest FFT implementation and can be overriden in gwnum.txt. */
#define gwset_bench_cores(h,n)		((h)->bench_num_cores = n)

/* Set this if it is known how many independent gwnum FFTs will be active -- either by your program or multiple instances of your program. */
/* Prime95 calls this "worker windows".  By default, this value is the number of cores divided by number of threads.  This setting */
/* may affect selection of fastest FFT implementation and can be overriden in gwnum.txt. */
#define gwset_bench_workers(h,n)	((h)->bench_num_workers = n)

/* Set this if FFTs will always error check, will error check if near limit of FFT, or will not error check.  This setting may affect */
/* selection of fastest FFT implementation.  By default, it is assumed round off error checking will not be used for every operation. */
#define gwset_will_error_check(h)		((h)->will_error_check = 1)
#define gwset_will_error_check_near_limit(h)	((h)->will_error_check = 2)
#define gwclear_will_error_check(h)		((h)->will_error_check = 0)

/* Prior to calling one of the gwsetup routines, you can have the library attempt to use large pages (2MB or 4MB on Intel architecture) */
/* rather than the standard 4KB pages.  This may improve performance by reducing TLB misses.  It may have system-wide costs as the OS */
/* may not page these to disk when not in use.  NOTE: Only the first gwalloc will return memory allocated using large pages. */
#define gwset_use_large_pages(h)	((h)->use_large_pages = 1)
#define gwclear_use_large_pages(h)	((h)->use_large_pages = 0)
#define gwget_use_large_pages(h)	((h)->use_large_pages)
#define gw_using_large_pages(h)		((h)->large_pages_ptr != NULL)

/* By default gwnum uses mutexes rather than spin waits to implement multithreading.  This macro lets you try spin waits to see if they are faster. */
/* A setting of one tells the main thread to spin wait on helper threads to finish up.  A setting above one tells the main thread to spin wait plus */
/* n-1 helper threads to spin wait on work to do.  NOTE:  Many, including Linus Torvalds, believe spin waits in user space is evil.  Read up on the */
/* hazards of spin waits at https://www.realworldtech.com/forum/?threadid=189711&curpostid=189723 and elsewhere.  That said, a system dedicated to */
/* running a program doing multithreaded gwnum work could see a benefit. */
#define gwset_use_spin_wait(h,n)	((h)->use_spin_wait = (char) (n))

/* Prior to calling one of the gwsetup routines, you must tell the gwnum library if the polymult library will also be used.  Using polymult can affect */
/* how much memory is allocated by each gwalloc call. */
#define gwset_using_polymult(h)		((h)->polymult = TRUE)

/* Prior to calling one of the gwsetup routines, you can tell the gwnum library to do a faster partial setup.  You won't be able to do any math */
/* operations, but you can call informational routines such as get FFT length, FFT description, gwnum size, etc. */
#define gwset_information_only(h)	((h)->information_only = TRUE)

/* DEPRECATED, Prior to calling one of the gwsetup routines, you can tell the library to use a hyperthread for memory prefetching. */
/* Only implemented for AVX-512 FFTs.  Caller must ensure the compute thread and prefetching hyperthread are set to use */
/* the same physical CPU core.  At present there are no known CPUs where this provides a benefit. */
#define gwset_hyperthread_prefetch(h)	//((h)->hyperthread_prefetching = TRUE)
#define gwclear_hyperthread_prefetch(h)	//((h)->hyperthread_prefetching = FALSE)

/* DEPRECATED, use gwset_minimum_fftlen instead. */
/* Prior to calling one of the gwsetup routines, you can force the library to use a specific fft length.  This should rarely (if ever) be used. */
/* I use it occasionally for benchmarking and/or checking round off errors at the FFT crossover points. */
/* Only choose a specific FFT size if you know what you are doing!! */
#define gwset_specific_fftlen(h,n)	((h)->minimum_fftlen = n)

/*---------------------------------------------------------------------+
|                     GWNUM MEMORY ALLOCATION ROUTINES                 |
+---------------------------------------------------------------------*/

/* Allocate memory for a gwnum */
gwnum gwalloc (
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/* Free (or cache) a previously allocated gwnum.  gwdata->gwnum_max_free_count (default 10) gwnums are cached. */
void gwfree (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	val);		/* Gwnum to free */

/* Allocate an array and fill it with gwnums.  Uses a little less memory than allocating an array and calling gwalloc many times. */
gwarray gwalloc_array (		/* Pointer to an array of gwnums */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	uint64_t n);		/* Size of the array of gwnums */

/* Free a previously allocated array of gwnums */
void gwfree_array (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwarray	array);		/* Array of gwnums to free */

/* Free all previously allocated gwnums */
void gwfreeall (
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/* Free internal memory that can be safely freed.  Call this prior to using a lot of gwnum memory (this also calls gwfree_cached to further reduce mem used). */
void gwfree_internal_memory (
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/* Empty cache of freed gwnums.  Call this to minimize gwnum library's memory footprint when no more gwallocs are anticipated anytime soon. */
void gwfree_cached (
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/* Highly specialized routine!  Allocates and initializes FFT(1).  Internally FFT(1) is used by gwmuladd4, gwmulsub4, and gwunfft.  Should */
/* the user also need FFT(1) this let's you avoid allocating a second gwnum that holds the same value. */
void gwuser_init_FFT1 (		/* Calculate GW_FFT1 at user's request */
	gwhandle *gwdata);	/* Handle initialized by gwsetup */

/*---------------------------------------------------------------------+
|                        GWNUM CONVERSION ROUTINES                     |
+---------------------------------------------------------------------*/

/* Convert a double (must be a positive integer) to a gwnum */
void dbltogw (gwhandle *, double, gwnum);
/* Convert a int64_t to a gwnum */
void s64togw (gwhandle *, int64_t, gwnum);
/* Convert a uint64_t to a gwnum */
void u64togw (gwhandle *, uint64_t, gwnum);

/* Convert a binary value (array of 32-bit values) to a gwnum */
void binarytogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const uint32_t *array,	/* Array containing the binary value */
	uint32_t arraylen,	/* Length of the array */
	gwnum	n);		/* Destination gwnum */

/* Convert a binary value (array of 64-bit values) to a gwnum */
void binary64togw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const uint64_t *array,	/* Array containing the binary value */
	uint64_t arraylen,	/* Length of the array */
	gwnum	n);		/* Destination gwnum */

/* Convert a gwnum to a binary value (array of 32-bit values).  Returns the number of 32-bit values written to the array.  The array is NOT zero-padded. */
/* Returns a negative number if an error occurs during the conversion.  An error can happen if the FFT data contains a NaN or infinity value. */
/* NOTE: uses gwtogiant which requires a buffer a few words larger than the maximum possible returned value.  Calling this routine with a buffer */
/* large enough for gwtogiant to use will save allocating the bigger buffer and a memcpy. */
long gwtobinary (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	uint32_t *array,	/* Array to contain the binary value */
	uint32_t arraylen);	/* Maximum size of the array */

/* Convert a gwnum to a binary value (array of 64-bit values).  Returns the number of 64-bit values written to the array.  The array is NOT zero-padded. */
/* Returns a negative number if an error occurs during the conversion.  An error can happen if the FFT data contains a NaN or infinity value. */
/* NOTE: uses gwtogiant which requires a buffer a few words larger than the maximum possible returned value.  Calling this routine with a buffer */
/* large enough for gwtogiant to use will save allocating the bigger buffer and a memcpy. */
long gwtobinary64 (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	uint64_t *array,	/* Array to contain the binary value */
	uint32_t arraylen);	/* Maximum size of the array */

/* Generate a random number.  Can be useful for QA purposes. */
void gw_random_number (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x);		/* Returned random number */

/*---------------------------------------------------------------------+
|                          GWNUM MATH OPERATIONS                       |
+---------------------------------------------------------------------*/

/* Macros and routines to interface with assembly code */

/* The assembly routines are designed to provide a flexible way of multiplying two numbers with lots of options to maximize performance. */
/* To get maximum performance, you'll need to look for ways to minimize transforms (if a value is used in several multiply operations, */
/* it is beneficial to perform the forward transform just once). */
/* Especially important for large numbers on modern bandwidth-limited CPUs is looking for ways to reduce memory accesses.  This can be done */
/* by having multiply operations start a forward transform on the result (doing this means a multiply requires two r/w accesses to memory */
/* instead of three).  Another technique is to use gwaddmul4 and gwsubmul4, which saves a r/w if the first two arguments are already transformed */
/* compared to a separate gwadd or gwsub followed by a gwmul.  Also, using options in gwmul3 to transform input arguments as a side effect of */
/* a multiply operation can save a r/w compared to a separate gwfft call followed by a gwmul3 call.  One thing to note on gwmul3 options: unless */
/* specifically instructed to preserve or transform an input argument, gwmul3 will either transform or leave the input argument unchanged.  */
/* Similarly, gwaddmul4 and gwubmul4 will either transform or leave unchanged the third input argument (the first two input arguments are always */
/* transformed).  Lastly, there are the more mundane methods of reducing memory accesses: perform operations "in-place" destination same as one */
/* of the source arguments), minimize gwcopy calls, etc. */
/* The last area of optimization is reducing normalizations in gwadd, gwsub, gwaddsub.  When working on numbers that are not near the maximum */
/* an FFT size can accommodate, add & sub operations can sometimes be performed without normalizing carries.  To do this gwnum needs to know how */
/* you intend to use the outputs of add & sub operations.  From worst-to-best (tell gwnum the worst usage) choose one of add/sub output will be */
/* used as input to: 1) gwsquare, 2) gwmul or third argument of gwaddmul/gwsubmul, 3) first/second argument of gwaddmul/gwsubmul.  Also note that */
/* gwadd bases it's normalization decisions assuming the input arguments are essentially random data.  Using gwadd to double a number is *not* random */
/* data as every FFT data element will double in magnitude.  There is a gwadd option to let gwnum know that this is a worst-case non-random scenario */
/* and act accordingly.  Lastly, if doing several gwadd/gwsub operations in a row use the option that delays normalization until the last gwadd/gwsub. */

/* Note that multiply routines allow one unnormalized addition prior to a multiply without introducing too much convolution error.  Thus: */
/* Legal:	gwaddquick (h, t1, t2); gwmul (h, t2, x); */
/* Legal:	gwfft (h, t1, t1); gwfft (h, t2, t2); gwadd (h, t1, t2); gwmul (h, t2, x); */
/* Not Legal:	gwaddquick (h, t1, t2); gwaddquick (h, y, x); gwmul (h, t2, x); */
/* Not Legal:	gwfft (h, t1, t1); gwadd (h, t1, t1); gwsquare (h, t1); */

/* A brief description of each of the commonly used "gw" routines.  The gwhandle argument is omitted.  Sorry for the unsightly "o" (which stands */
/* for "with options") following gwadd3, gwsub3, gwaddsub4.  The non-"o" name was used in an earlier gwnum and macros are in place to convert */
/* the old usage to the new usage. */

/* gwswap(a,b)			Quickly swaps two gw numbers */
/* gwcopy(s,d)			Copies gwnum s to d */
/* gwfft(s,d)			Perform the forward Fourier transform on a number */
/* gwsquare2(s,d)		Computes d=s*s */
/* gwmul3(s1,s2,d)		Computes d=s1*s2 with many options available */
/* gwmul3_carefully(s1,s2,d)	Like gwmul3 but uses a slower method that will have a lower roundoff error even if input is non-random. */
/* gwaddmul4(s1,s2,s3,d)	Computes d=(s1+s2)*s3 with many options available */
/* gwsubmul4(s1,s2,s3,d)	Computes d=(s1-s2)*s3 with many options available */
/* gwadd3o(s1,s2,d)		Adds two numbers (with options).  Output is normalized them if necessary (only if inputs not FFTed) */
/* gwsub3o(s1,s2,d)		Subtracts second number from first number (with options).  Output is normalized them if necessary (if inputs not FFTed) */
/* gwaddsub4o(s1,s2,d1,d2)	Adds and subtracts 2 numbers (first+second and first-second). normalizes the results if necessary */

/* Macros to do easy work */
#define gwswap(s,d)		{gwnum t; t = s; s = d; d = t;}
#define gwsquare2(h,s,d,opt)	gwmul3 (h,s,s,d,opt)

/* A global setting is used to determine if roundoff error-checking is performed by the gwnum multiplication routines. */
/* The maximum roundoff error can be retrieved using gw_get_maxerr. */
void gwerror_checking (gwhandle *, int e);		/* e is true or false for roundoff error checking on/off */

/* Set the small constant which the results of a multiplication can be multiplied by. */
/* Use this routine in conjunction with the GWMUL_MULBYCONST option. */
#define GWMULBYCONST_MAX	255		/* I think this is right */
void gwsetmulbyconst (gwhandle *gwdata, long s);

void gwcopy (			/* Copy a gwnum */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source */
	gwnum	d);		/* Dest */

void gwfft (			/* Forward FFT */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d);		/* Destination (can be same as source) */

void gwfft_for_fma (		/* Forward FFT with extra processing for use in gwmuladd4 or gwmulsub4.  Only use this routine if */
				/* destination will be used many times as the third source argument to gwmuladd4 or gwmulsub4. */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s,		/* Source number */
	gwnum	d);		/* Destination (can overlap source) */

/* Options for gwmul3, gwaddmul4, gwsubmul4, gwmuladd4, gwmulsub4 routines */
#define GWMUL_FFT_S1		0x0001		/* FFT the first source */
#define GWMUL_PRESERVE_S1	0x0002		/* Do not modify the first source */
#define GWMUL_FFT_S2		0x0004		/* FFT the second source */
#define GWMUL_PRESERVE_S2	0x0008		/* Do not modify the second source */
#define GWMUL_FFT_S3		0x0010		/* FFT the third source */
#define GWMUL_PRESERVE_S3	0x0020		/* Do not modify the third source */
#define GWMUL_FFT_S4		0x0040		/* FFT the fourth source */
#define GWMUL_PRESERVE_S4	0x0080		/* Do not modify the fourth source */
#define GWMUL_ADDINCONST	0x0100		/* Addin the optional gwsetaddin value to the multiplication result */
#define GWMUL_MULBYCONST	0x0200		/* Multiply the final result by the small mulbyconst */
#define GWMUL_STARTNEXTFFT	0x0400		/* Start the forward FFT of the multiplication result (for better performance) */
/* These "combo" options are provided to make code more readable */
#define GWMUL_FFT_S12		(GWMUL_FFT_S1 | GWMUL_FFT_S2)
#define GWMUL_FFT_S13		(GWMUL_FFT_S1 | GWMUL_FFT_S3)
#define GWMUL_FFT_S14		(GWMUL_FFT_S1 | GWMUL_FFT_S4)
#define GWMUL_FFT_S23		(GWMUL_FFT_S2 | GWMUL_FFT_S3)
#define GWMUL_FFT_S24		(GWMUL_FFT_S2 | GWMUL_FFT_S4)
#define GWMUL_FFT_S34		(GWMUL_FFT_S3 | GWMUL_FFT_S4)
#define GWMUL_FFT_S123		(GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_FFT_S3)
#define GWMUL_FFT_S124		(GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_FFT_S4)
#define GWMUL_FFT_S134		(GWMUL_FFT_S1 | GWMUL_FFT_S3 | GWMUL_FFT_S4)
#define GWMUL_FFT_S234		(GWMUL_FFT_S2 | GWMUL_FFT_S3 | GWMUL_FFT_S4)
#define GWMUL_FFT_S1234		(GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_FFT_S3 | GWMUL_FFT_S4)
/* There are advanced GWMUL_ options described later */
void gwmul3 (			/* Multiply two gwnums, one of s1 or s2 will be FFTed unless the PRESERVE option is set */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options);
void gwmul3_carefully (		/* Multiply two gwnums where inputs may not be random data */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	d,		/* Destination */
	int	options);
void gwaddmul4 (		/* (s1+s2)*s3, s1 and s2 will be FFTed unless the PRESERVE option is set */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options);
void gwsubmul4 (		/* (s1-s2)*s3, s1 and s2 will be FFTed unless the PRESERVE option is set */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options);
void gwmuladd4 (		/* (s1*s2)+s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options);
void gwmulsub4 (		/* (s1*s2)-s3 */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	d,		/* Destination */
	int	options);
void gwmulmuladd5 (		/* Calculate (s1*s2)+(s3*s4) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	s4,		/* Fourth source */
	gwnum	d,		/* Destination */
	int	options);	/* See gwnum.h */
void gwmulmulsub5 (		/* Calculate (s1*s2)-(s3*s4) */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* First source */
	gwnum	s2,		/* Second source */
	gwnum	s3,		/* Third source */
	gwnum	s4,		/* Fourth source */
	gwnum	d,		/* Destination */
	int	options);	/* See gwnum.h */

/* Simplified options for gwadd3o, gwsub3o, gwaddsub4o routines */
/* This is complicated stuff.  Reading tutorial.txt is highly recommended! */
#define GWADD_SQUARE_INPUT		0x0001	/* Result will eventually be input to gwsquare */
#define GWADD_MANY_INPUTS		0x0001	/* Result will be input to gwmul or gwaddmul where multiple arguments come from the output of gwadd */
#define GWADD_MUL_INPUT			0x0002	/* Result will eventually be input to gwmul3 */
#define GWADD_ADD_INPUT			0x0004	/* Result will eventually be input to one of the first two arguments of gwaddmul4 or gwsubmul4 */
#define GWADD_NON_RANDOM_DATA		0x0010	/* Two add inputs are correlated (like adding number to itself) which has much worse impact on roundoff */
#define GWADD_GUARANTEED_OK		0x2000	/* Do not normalize the result.  Treat result like a fully normalized number. */
#define GWADD_DELAY_NORMALIZE		0x4000	/* Do not normalize the result (another add operation is coming) */
#define GWADD_FORCE_NORMALIZE		0x8000	/* Force normalization of the result */
/* There are advanced GWADD_ options described later.  For GWADD_MANY_INPUTS there are cases where GWADD_FORCE_NORMALIZE or the advanced */
/* GWADD_ options are required for proper execution.  Again, reading and re-reading tutorial.txt is highly recommended! */
void gwadd3o (			/* Add two numbers normalizing if needed and inputs are not FFTed. */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	s2,		/* Source #2.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	d,		/* Destination */
	int	options);
void gwsub3o (			/* Compute s1 - s2 normalizing if needed and inputs are not FFTed. */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	s2,		/* Source #2.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	d,		/* Destination */
	int	options);
void gwaddsub4o (		/* Add & sub two nums normalizing if needed and inputs are not FFTed. */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	s1,		/* Source #1.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	s2,		/* Source #2.  Will be FFTed if two sources do not have the same FFT state. */
	gwnum	d1,		/* Destination #1 */
	gwnum	d2,		/* Destination #2 */
	int	options);

/* The FFT selection code assumes FFT data will essentially be random data2 yielding pretty well understood maximum */
/* round off errors.  When working  with some numbers, especially at the start of a PRP exponentiation, the FFT data */
/* is decidedly not random, leading to much larger than expected roundoff errors.  In my own PRP code, I call */
/* gwsquare_carefully for the first 30 iterations.  To make this easier (and code more readable) you can call this */
/* routine and the next n gwsquare or gwmul3 calls will be replaced by gwmul3_carefully calls.  If you pass an n of -1, */
/* the gwnum code will use a default value for n that should be suitable for getting a PRP exponentiation into a */
/* "random data state".  This routine can be called before gwsetup is called. */
void gwset_carefully_count (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	int	n);		/* Number of squarings and multiplications to do carefully. */
				/* If n is -1, a suitable default value is used */

/* This routine is used to add a constant to the result of a multiplication at virtually no cost.  Prime95 uses this routine to do the -2 operation in */
/* a Lucas-Lehmer test.  NOTE:  There are some number formats where the add-in must be emulated and is thus expensive.  Namely, Montgomery reduction or */
/* if k != 1 and abs(c) != 1 in k*b^n+c or then emulation occurs.  If you also use the mul-by-const option, the multiply is done after the addition. */
/* I think an add-in as large as 40 or 45 bits ought to work, but this is not tested. */
void gwsetaddin (gwhandle *, long);
/* Returns TRUE if gwsetaddin and GWMUL_ADDINCONST must be emulated with a gwmuladd4.  Caller may have a faster alternative if this is the case. */
#define is_gwsetaddin_emulated(h)	((h)->k != 1.0 && labs ((h)->c) != 1)
/* Prime95 uses this specialized gwsetaddinatpowerofb.  k must be 1 and abs(c) must be 1.  This is not emulated. */
void gwsetaddinatpowerofb (gwhandle *, long, unsigned long);
/* Similar to gwsetaddin, this routine adds a very small constant to the result of a multiplication at virtually no cost.  This routine is only useful */
/* if you need to use both GWMUL_ADDINCONST and GWMUL_MULBYCONST.  The addin is done after the mul-by-const operation.  WARNING: The addition is done */
/* after carry propagation has occurred.  Furthermore, for numbers of the k*b^n+/-1 the addin may not be applied to the least significant bits of the */
/* FFT word.  If the addin value exceeds the "b" in k*b^n+c, then you risk dangerous round off errors. */
void gwsetpostmulbyconstaddin (gwhandle *, int);

/* This routine adds a small value to a gwnum.  This lets us apply some optimizations that cannot be performed by general purpose gwadd. */
void gwsmalladd (gwhandle *gwdata, int64_t addin, gwnum g);

/* This routine multiplies a gwnum by a small positive value.  This lets us apply some */
/* optimizations that cannot be performed by a full FFT multiplication. */
#define GWSMALLMUL_MAX		67108864.0		/* May allow more at a later date */
void gwsmallmul (gwhandle *gwdata, double mult, gwnum g);

/* Perform an inverse FFT.  This may be inefficient!!  Call this to undo a forward FFT performed on a gwnum where unFFTed data is needed. */
/* In a perfect world, the forward FFT would not have been done in the first place.  However, there are counter examples.  Plus, the new */
/* polymult library returns FFTed data that needs normalizing before it can be used in future operations.  In fact, polymult users may */
/* benefit from using the GWMUL_STARTNEXTFFT option! */
void gwunfft (gwhandle *, gwnum s, gwnum d);
void gwunfft2 (gwhandle *, gwnum s, gwnum d, int options);

/* Obscure routine to possibly keep a gwnum in the CPU caches by accessing it. */
#define gwtouch(h,s)		gwcopy (h,s,s)

/*---------------------------------------------------------------------+
|                      GWNUM ERROR-CHECKING ROUTINES                   |
+---------------------------------------------------------------------*/

#define gw_test_for_error(h)		((h)->GWERROR)
#define gw_test_illegal_sumout(h)	((h)->GWERROR & 1)
#define gw_clear_error(h)		((h)->GWERROR = 0)

/* Get or clear the roundoff error.  Remember that if the roundoff error exceeds 0.5 then the FFT results will be wrong. */
/* It is prudent to watch the roundoff error to make sure the roundoff error does not get close to 0.5. */
double gw_get_maxerr (gwhandle *gwdata);
void gw_clear_maxerr (gwhandle *gwdata);

/* Return TRUE if we are operating near the limit of this FFT length.  Input argument is the percentage to consider as near the limit. */
/* For example, if percent is 0.1 and the FFT can handle 20 bits per FFT data word, then if there are more than 19.98 bits per FFT data */
/* word this function will return TRUE. */
int gwnear_fft_limit (gwhandle *gwdata, double pct);

/* Returns true if the current FFT length satisfies the given safety margin (such as the value returned by polymult_safety_margin) */
#define gw_passes_safety_margin(h,safetyval)		((h)->EXTRA_BITS/2.0f > (safetyval))

/* Set the safety margin for upcoming polymult operations */
void gwset_polymult_safety_margin (gwhandle *gwdata, float safetyval);

/*---------------------------------------------------------------------+
|                    GWNUM MISC. INFORMATION ROUTINES                  |
+---------------------------------------------------------------------*/

/* Map a gwerror code into human readable text */
void gwerror_text (gwhandle *gwdata, int error_code, char *buf, int buflen);

/* Return TRUE if this is a GPL'ed version of the GWNUM library. */
#define gwnum_is_gpl()		(0)

/* Return the FFT length being used */
#define gwfftlen(h)		((h)->FFTLEN)

/* Generate a human-readable description of the chosen FFT length and type */
void gwfft_description (gwhandle *, char *buf);

/* A human-readable string for the modulus currently in use */
#define gwmodulo_as_string(h)	((h)->GWSTRING_REP)

/* Gwnum keeps a running count of the number of Fast Fourier transforms performed.  You can get and reset this counter.  For non-transformed */
/* inputs, a squaring requires one forward and one inverse transform, and a multiply requires two forward and one inverse transforms. */
uint64_t gw_get_fft_count (gwhandle *);
void gw_clear_fft_count (gwhandle *);

/* Get the amount of memory needed to allocate a gwnum.  This includes FFT data, headers, and pad bytes for alignment. */
unsigned long gwnum_size (gwhandle *);

/* Get the fixed amount of memory allocated during gwsetup.  Programs can use this and gwnum_size to determine working set size and act accordingly. */
unsigned long gwmemused (gwhandle *);

/* Return TRUE if using gwmuladd4 and gwmulsub4 results in an extra gwnum being allocated */
#define gwfma_will_alloc_a_gwnum(h)	((h)->FFT1_state != 2)

/* Returns TRUE if the gwnum value is normalized (the gwadd did not do an unnormalized add) */
#define gwnum_is_normalized(h,g)	(unnorms(g) == 0.0f)

/* Returns TRUE if the gwnum value is in the specified FFT state */
#define gwnum_is_not_ffted(h,g)		(FFT_state(g) == NOT_FFTed)
#define gwnum_is_partially_ffted(h,g)	(FFT_state(g) == PARTIALLY_FFTed)
#define gwnum_is_fully_ffted(h,g)	(FFT_state(g) == FULLY_FFTed)
#define gwnum_is_ffted_for_fma(h,g)	(FFT_state(g) == FFTed_FOR_FMA)

/* Macros to acccess some of the header values in a gwnum.  Do not access or change these unless you know what you are doing! */
/* Unnorms is the number of unnormalized adds that have taken place in creating a gwnum.  The add/sub routines do not always */
/* normalize results, it depends on how close k*b^n+c is to the maximum for this FFT size.  Add/sub of FFTed or partially FFTed data */
/* never normalizes results.  It is the programmer's responsibility to properly use the GWADD options to ensure too many unnormalized add/sub */
/* operations occur prior to a multiplication operation.  ASSERTs can be enabled to ensure excessive unnormalized adds are not occurring. */
#define FFT_state(x)		((uint32_t *)(x))[-7]
#define	NOT_FFTed		0
#define PARTIALLY_FFTed		1
#define FULLY_FFTed		3
#define	FFTed_FOR_FMA		4
#define unnorms(x)		((float *)(x))[-1]

/* Get the amount of memory required for the gwnum's raw FFT data.  This does not include the GW_HEADER_SIZE bytes for the header */
/* or any pad bytes that might be allocated for alignment.  I see little need for a program to use this routine. */
#define gwnum_datasize(h)	(h)->datasize

/*-----------------------------------------------------------------+
|               ADVANCED GWADD_ and GWMUL_ options                 |
+-----------------------------------------------------------------*/

/* These advanced options are described in tutorial.txt */

#define GWADD_NORMALIZE_IF(b)		((b) ? GWADD_FORCE_NORMALIZE : GWADD_DELAY_NORMALIZE)
#define GWADD_DELAYNORM_IF(b)		GWADD_NORMALIZE_IF(!(b))
#define GWMUL_STARTNEXTFFT_IF(b)	((b) ? GWMUL_STARTNEXTFFT : 0)

/* The above three macros are combined with one of the following macros which return TRUE or FALSE depending on whether gwsquare, */
/* gwmul, gwaddmul, or gwmuladd will be safe if the arguments to those routines have the designated number of unnormalized adds. */
/* Note there are two different macros for gwmuladd.  If S1 != S2 use muladd_safe, otherwise use squareadd_safe */

#define square_safe(h,numadds1)				((h)->EXTRA_BITS >= EB_GWMUL_SAVINGS + numadds_to_eb(numadds1) * 2.0f)
#define mul_safe(h,numadds1,numadds2)			((h)->EXTRA_BITS >= numadds_to_eb(numadds1) + numadds_to_eb(numadds2))
#define addmul_safe(h,numadds1,numadds2,numadds3)	mul_safe(h,(numadds1)+(numadds2)+1,numadds3)
#define muladd_safe(h,numadds1,numadds2,numadds3)	(FFT1_is_simple(h) ? mul_safe(h,numadds1,numadds2) : mulmuladd_safe(h,numadds1,numadds2,0,numadds3))
#define squareadd_safe(h,numadds1,numadds2,numadds3)	(FFT1_is_simple(h) ? square_safe(h,numadds1) : squaremuladd_safe(h,numadds1,numadds2,0,numadds3))
#define FFT1_is_simple(h)				(!(h)->GENERAL_MMGW_MOD && ((h)->k == 1.0 || labs((h)->c) == 1))

/* These macros are for gwmulmuladd safety calculations.  If s1 == s2 or s3 == s4, then the gwmulmuladd is doing squarings which behaves */
/* differently in safety calculations.  Thus, there are four macros to handle the different gwmulmuladd possibilities. */

#define mulmuladd_safe(h,adds1,adds2,adds3,adds4)	((h)->EXTRA_BITS >= mma5_pairs_eb((adds1)+(adds2),(adds3)+(adds4)))
#define squaremuladd_safe(h,adds1,adds2,adds3,adds4)	((h)->EXTRA_BITS >= mma5_sqr_penalty+mma5_pairs_eb(2*(adds1)+1,2*(adds3)))
#define mulsquareadd_safe(h,adds1,adds2,adds3,adds4)	((h)->EXTRA_BITS >= mma5_sqr_penalty+mma5_pairs_eb(2*(adds1),2*(adds3)+1))
#define squaresquareadd_safe(h,adds1,adds2,adds3,adds4)	((h)->EXTRA_BITS >= 2*mma5_sqr_penalty+mma5_pairs_eb(2*(adds1)+1,2*(adds3)+1))
#define mma5_pairs_eb(sum1,sum2)			((sum1)<(sum2)?numadds_to_eb(sum1)+numadds_to_eb((sum2)+1):numadds_to_eb((sum1)+1)+numadds_to_eb(sum2))	// Add one to larger pair
#define mma5_sqr_penalty				(EB_GWMUL_SAVINGS - EB_FIRST_ADD)

/*-----------------------------------------------------------------+
|             **DANGEROUS**  GWNUM COMPARISON ROUTINES             |
+-----------------------------------------------------------------*/

/* Test if a gwnum is zero.  This routine was originally written by Jean Penne. */
/* It has not been adequately tested and MAY NOT BE BUG-FREE.  Use at your own risk! */
/* Returns TRUE if number is zero, FALSE if number is not zero, and a negative error */
/* code if a problem is found. */
int gwiszero (gwhandle *, gwnum);

/* Test two gwnums for equality.  Written by Jean Penne.  Uses the gwiszero routine */
/* which MAY NOT BE BUG-FREE.  Use this routine at your own risk! */
/* Returns TRUE if number is zero, FALSE if number is not zero, and a negative error */
/* code if a problem is found. */
int gwequal (gwhandle *, gwnum, gwnum);

/*---------------------------------------------------------------------+
|                          CLONING ROUTINES                            |
+---------------------------------------------------------------------*/

/* Experimental routine to clone a gwhandle.  The cloned handle uses less resources than a full gwsetup by sharing many data structures with */
/* the original handle.  The cloned handle can be used in a fairly unlimited way in another thread.  Valid operations in the cloned handle are single */
/* threaded multiplication, addition, subtraction.  Many other operations will work as well. */
/* NOTE: All memory allocated by gwalloc will be "owned" by the parent gwdata.  These allocated gwnums can be used or freed by the parent gwdata or */
/* any of the cloned gwdatas. */
int gwclone (
	gwhandle *cloned_gwdata,	/* Empty handle to be populated */
	gwhandle *gwdata);		/* Handle to clone */

/* Merge various stats (MAXERR, fft_count, etc.) back into the parent gwdata.  This routine does not do any locking to make sure the */
/* parent gwdata is not busy nor are any other cloned gwdatas simultaneously merging stats.  Locking is the caller's responsibility. */
void gwclone_merge_stats (
	gwhandle *dest_gwdata,		/* Handle to a (possibly cloned) gwdata to merge stats into */
	gwhandle *cloned_gwdata);	/* Handle to a cloned gwdata to merge stats from */

/*---------------------------------------------------------------------+
|                 ALTERNATIVE INTERFACES USING GIANTS                  |
+---------------------------------------------------------------------*/

/* The giants library from the late Dr. Richard Crandall at Perfectly Scientific is used internally for a few infrequent operations. */
/* It can optionally be used in the interfaces to convert between gwnum data type and binary.  I do not recommend this.  There are many */
/* other faster and more robust bignum libraries available.  GMP is very, very good.  I shied away from it here because of the GPL license. */
#include "giants.h"

/* Same as gwsetup_general_mod but uses giants instead of array of longs */
int gwsetup_general_mod_giant (
	gwhandle *gwdata,	/* Placeholder for gwnum global data */
	giant n);		/* The modulus */

/* Convert a giant to a gwnum */
void gianttogw (gwhandle *, giant, gwnum);

/* Convert a gwnum to a giant.  WARNING: Caller must allocate an array that is several words larger than the maximum result */
/* that can be returned.  This is a gross kludge that lets gwtogiant use the giant for intermediate calculations. */
/* Returns a negative number if an error occurs.  Returns zero on success. */
int gwtogiant (gwhandle *, gwnum, giant);

/*---------------------------------------------------------------------+
|          MISC. CONSTANTS YOU PROBABLY SHOULDN'T CARE ABOUT           |
+---------------------------------------------------------------------*/

/* The maximum value k * mulbyconst can be in a zero pad FFT.  Larger values must use generic modular reduction. */
#define MAX_ZEROPAD_K	2251799813685247.0	/* 51-bit k's are OK. */

/* The maximum value c * mulbyconst can be in a zero pad FFT.  Larger values must use generic modular reduction. */
#define MAX_ZEROPAD_C	8388607			/* 23-bit c's seem to work. */

/* Constants for calculating EXTRA_BITS.  These are based on data from the (not-included) program roe_gwnum.cpp.  This program output: */
// Roe for 2M FFT, threads = 2
// FFT: FMA3 FFT length 2M, Pass1=2K, Pass2=1K, clm=1, 2 threads
// gwsquare, avg roe: 0.228303
// gwmul3, avg roe: 0.158434
// Addq, gwsquare, avg roe: 0.457921
// addmul4, avg roe: 0.225439
// Addq mul input, addmul4, avg roe: 0.319609
// Addq add input, addmul4, avg roe: 0.275238
// Addq addq one add input, addmul4, avg roe: 0.320079
// Addq both add inputs, addmul4, avg roe: 0.321272
// Addq one add input, addq mul input, addmul4, avg roe: 0.393078
//
// Which leads us to conclude:
//
// Output bits saved from gwmul vs. gwsquare:  log2(.158434/.228303) = -.5271
// Impact of addquick before gwsquare: log2(.457921/.228303) = 1.004 bits (or 0.502 bits per gwmul input)
// Impact of addquick on gwmul: log2(0.225439/0.158434) = 0.5089
// and another = log2(0.319609/0.225439) = 0.5036
// and another = log2(0.393078/0.275238) = 0.5141
// Impact of second addquick: log2(.275238/.225439) = 0.2879 bits
// Impact of third addquick: log2(.320079/.275238) = 0.2177 bits

#define EB_GWMUL_SAVINGS	0.5271f
#define EB_FIRST_ADD		0.5089f
#define EB_SECOND_ADD		0.2879f
#define EB_THIRD_ADD		0.2177f

// Convert count of unnormalized adds to and from extra FFT output bits
#define numadds_to_eb(n)	((n) > 2.0f ? EB_FIRST_ADD + EB_SECOND_ADD + ((n) - 2.0f) * EB_THIRD_ADD : \
				 (n) > 1.0f ? EB_FIRST_ADD + EB_SECOND_ADD : (n) > 0.0f ? EB_FIRST_ADD : 0.0f)
#define eb_to_numadds(n)	((n) > EB_FIRST_ADD + EB_SECOND_ADD ? 2.0f + ((n) - EB_FIRST_ADD - EB_SECOND_ADD) / EB_THIRD_ADD : \
				 (n) > EB_FIRST_ADD ? 2.0f : (n) > 0.0f ? 1.0f : 0.0f)

/*---------------------------------------------------------------------+
|                        OLDER GWNUM INTERFACE                         |
+---------------------------------------------------------------------*/

/* gwadd(s,d)			DEPRECATED - use gwadd3.  Adds two numbers and normalizes result if necessary */
/* gwsub(s,d)			DEPRECATED - use gwsub3.  Subtracts first number from second number and normalizes result if necessary */
/* gwaddsub(x,y)		DEPRECATED - use gwaddsub4.  Adds and subtracts 2 numbers (x+y and x-y) normalizes the results if necessary */
/* gwfftadd(s,d)		DEPRECATED - use gwadd3.  Adds two FFTed numbers */
/* gwfftsub(s,d)		DEPRECATED - use gwsub3.  Subtracts first FFTed number from second FFTed number */
/* gwfftaddsub(x,y)		DEPRECATED - use gwaddsub4.  Adds and subtracts 2 FFTed numbers */
/* gwfftadd3(s1,s2,d)		DEPRECATED - use gwadd3.  Adds two FFTed numbers */
/* gwfftsub3(s1,s2,d)		DEPRECATED - use gwadd3.  Subtracts second FFTed number from first FFTed number */
/* gwfftaddsub4(s1,s2,d1,d2)	DEPRECATED - use gwaddsub4.  Like gwfftaddsub but stores results in separate variables */
/* gwsquare(x)			DEPRECATED - use gwsquare2.  Shortcut for gwsquare2(x,x,0) */
/* gwsquare2(h,s,d) w/o options	DEPRECATED - use the new gwsquare2 and specify the proper options argument (or use gwsquare2_deprecated macro) */
/* gwmul(s,d)			DEPRECATED - use gwmul3.  Computes d=s*d.  NOTE: s is replaced by its FFT */
/* gwsafemul(s,d)		DEPRECATED - use gwmul3.  Like gwmul but s is not replaced with its FFT */
/* gwfftmul(s,d)		DEPRECATED - use gwmul3.  Computes d=s*d.  NOTE: s must have been previously FFTed */
/* gwfftfftmul(s1,s2,d)		DEPRECATED - use gwmul3.  Computes d=s1*s2.  Both s1 and s2 must have been previously FFTed */
/* gwsquare_carefully(x)	DEPRECATED - use gwmul3_carefully */
/* gwsquare2_carefully(s,d)	DEPRECATED - use gwmul3_carefully */
/* gwmul_carefully(s,d)		DEPRECATED - use gwmul3_carefully */
/* gwadd3(s1,s2,d)		DEPRECATED - use gwadd3o.  Adds two numbers */
/* gwsub3(s1,s2,d)		DEPRECATED - use gwsub3o.  Subtracts second number from first */
/* gwaddsub4(s1,s2,d1,d2)	DEPRECATED - use gwaddsub4o.  Like gwaddsub4o */
/* gwadd3quick(s1,s2,d)		DEPRECATED - use gwadd3o.  Adds two numbers WITHOUT normalizing */
/* gwsub3quick(s1,s2,d)		DEPRECATED - use gwsub3o.  Subtracts second number from first WITHOUT normalizing */
/* gwaddsub4quick(s1,s2,d1,d2)	DEPRECATED - use gwaddsub4o.  Like gwaddsub4 but WITHOUT normalizing */
/* force_normalize(x)		DEPRECATED - use GWADD_FORCE_NORMALIZE option.  Was used on destination of gwadd3, gwsub3, or gwaddsub4 in version 30.4 */

/* Macros to implement deprecated routines */

#define oldmulbyconst(h)		((h)->GLOBAL_MULBYCONST ? GWMUL_MULBYCONST : 0)	/* use deprecated gwsetnormroutine to multiply result by mulbyconst */
#define oldstartnextfft(h)		((h)->GLOBAL_POSTFFT ? GWMUL_STARTNEXTFFT : 0)	/* use deprecated gwstartnextfft for starting next forward FFT */

#define gwsquare(h,s)			gwsquare2 (h,s,s,oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwsquare2_deprecated(h,s,d)	gwsquare2 (h,s,d,oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwmul(h,s,d)			gwmul3(h,s,d,d,GWMUL_FFT_S1 | oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwsafemul(h,s,d)		gwmul3(h,s,d,d,GWMUL_PRESERVE_S1 | oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwfftmul(h,s,d)			gwmul3(h,s,d,d,oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwfftfftmul(h,s1,s2,d)		gwmul3(h,s1,s2,d,oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwsquare_carefully(h,x)		gwmul3_carefully(h,x,x,x,oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwsquare2_carefully(h,s,d)	gwmul3_carefully(h,s,s,d,GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2 | oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwmul_carefully(h,s,d)		gwmul3_carefully(h,s,d,d,GWMUL_PRESERVE_S1 | oldstartnextfft(h) | oldmulbyconst(h) | GWMUL_ADDINCONST)
#define gwset_square_carefully_count(h,n) DEPRECATED - use gwset_carefully_count

#define gwadd(h,s,d)			gwadd3 (h,s,d,d)
#define gwsub(h,s,d)			gwsub3 (h,d,s,d)
#define gwaddsub(h,a,b)			gwaddsub4 (h,a,b,a,b)
#define gwaddquick(h,s,d)		gwadd3quick (h,s,d,d)
#define gwsubquick(h,s,d)		gwsub3quick (h,d,s,d)
#define gwaddsubquick(h,a,b)		gwaddsub4quick (h,a,b,a,b)
#define gwfftadd(h,s,d)			gwadd3 (h,s,d,d)
#define gwfftsub(h,s,d)			gwsub3 (h,d,s,d)
#define gwfftaddsub(h,a,b)		gwaddsub4 (h,a,b,a,b)
#define gwfftadd3(h,s1,s2,d)		gwadd3 (h,s1,s2,d)
#define gwfftsub3(h,s1,s2,d)		gwsub3 (h,s1,s2,d)
#define gwfftaddsub4(h,s1,s2,d1,d2)	gwaddsub4 (h,s1,s2,d1,d2)
#define gwadd3(h,s1,s2,d)		gwadd3o (h,s1,s2,d,GWADD_MUL_INPUT)
#define gwsub3(h,s1,s2,d)		gwsub3o (h,s1,s2,d,GWADD_MUL_INPUT)
#define gwaddsub4(h,s1,s2,d1,d2)	gwaddsub4o (h,s1,s2,d1,d2,GWADD_MUL_INPUT)
#define gwadd3quick(h,s1,s2,d)		gwadd3o (h,s1,s2,d,GWADD_DELAY_NORMALIZE)
#define gwsub3quick(h,s1,s2,d)		gwsub3o (h,s1,s2,d,GWADD_DELAY_NORMALIZE)
#define gwaddsub4quick(h,s1,s2,d1,d2)	gwaddsub4o (h,s1,s2,d1,d2,GWADD_DELAY_NORMALIZE)
#define force_normalize(x)		DEPRECATED - use GWADD_FORCE_NORMALIZE

/* DEPRECATED.  The gwsetup routines pick the fastest FFT implementation by default.  Setting this option will cause gwsetup to give */
/* preference to FFT implementations that support the SUM(INPUTS) != SUM(OUTPUTS) error check.  GEC makes this option obsolete. */
/* NOTE: This error check was not available for k*b^n+c IBDWT FFTs when c is positive.  Setting this option had no effect. */
/* NOTE: sum_inputs checking was only available in SSE2 FFTs and earlier. */
#define gwset_sum_inputs_checking(h,b)
#define gw_test_mismatched_sums(h)	FALSE
#define gwsuminp(h,g)			((g)[-2])
#define gwsumout(h,g)			((g)[-3])

/* DEPRECATED.  Using gwerror_checking macro and the GWMUL_MULBYCONST option is preferred. */
/* The multiplication code has two options that you can set using this macro.  The e argument tells the multiplication */
/* code whether or not it should perform round-off error checking - returning the maximum difference from an integer result */
/* in MAXERR.  The c argument tells the multiplication code whether or not it should multiply the result by a small constant. */
/* These are global settings.  The c argument can be overridden in each each multiply call with GWMUL_NOMULBYCONST or GWMUL_MULBYCONST */
#define gwsetnormroutine(h,z,e,c) {(h)->GLOBAL_MULBYCONST=(c)?1:0;gwerror_checking(h,e);}

/* DEPRECATED.  Use GWMUL_STARTNEXTFFT options instead. */
/* If you know the result of a multiplication will be the input to another multiplication (but not gwmul_carefully), */
/* then a small performance gain can be had in larger FFTs by doing some of the next forward FFT at the end of the multiplication. */
/* Use this routine to tell the multiplication code whether or not it can start the forward FFT on the result. */
/* NOTE:  The STARTNEXTFFT option is not supported for generic modular reduction and one-pass FFTs. */
#define gwstartnextfft(h,state)	{(h)->GLOBAL_POSTFFT = (state);}

/* DEPRECATED!!! These routines were deprecated because unlike all other gwnum routines the destination argument appeared before the source argument. */
#define gwaddsmall(h,g,a) gwsmalladd(h,a,g)	
#define gwmulsmall(h,g,m) gwsmallmul(h,m,g)
#define GWSMALLADD_MAX		1125899906842624	/* 2^50 -- gwsmalladd now takes a int64_t and acceptss all values */

/* DEPRECATED.  Replaced by better named gwsetaddinatpowerofb */
#define gwsetaddinatbit(h,v,b)	gwsetaddinatpowerofb(h,v,b)

/* DEPRECATED -- replaced by more appropriately named macro */
#define norm_count(h)	unnorms(h)

/* DEPRECATED -- replaced by more appropriately named structure member */
#define ALL_COMPLEX_FFT	NEGACYCLIC_FFT

/* Convert a binary value (array of 32-bit or 64-bit values) to a gwnum.  Check your C compiler specs to see if a long is 32 or 64 bits. */
/* Use of this routine is HIGHLY DISCOURAGED.  It can lead to portability problems between Linux and Windows. */
void binarylongstogw (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	const unsigned long *array, /* Array containing the binary value */
	unsigned long arraylen,	/* Length of the array */
	gwnum	n);		/* Destination gwnum */

/* Convert a gwnum to a binary value (array of 32-bit or 64-bit values).  Check your C compiler specs to see if a long is 32 or 64 bits. */
/* Use of this routine is HIGHLY DISCOURAGED.  It can lead to portability problems between Linux and Windows. */
long gwtobinarylongs (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	n,		/* Source gwnum */
	unsigned long *array,	/* Array to contain the binary value */
	unsigned long arraylen);/* Maximum size of the array */

/*---------------------------------------------------------------------+
|          SPECIAL ECM ROUTINE FOR GMP-ECM USING GWNUM LIBRARY         |
+---------------------------------------------------------------------*/

/* Return codes */
#define ES1_SUCCESS		0	/* Success, but no factor */
#define ES1_FACTOR_FOUND	1	/* Success, factor found */
#define ES1_CANNOT_DO_IT	2	/* This k,b,n,c cannot be handled */
#define ES1_MEMORY		3	/* Out of memory */
#define ES1_INTERRUPT		4	/* Execution interrupted */
#define ES1_CANNOT_DO_QUICKLY	5	/* Requires 3-multiply reduction */
#define ES1_HARDWARE_ERROR	6	/* An error was detected, most likely a hardware error. */

/* Option codes */
#define ES1_DO_SLOW_CASE	0x1	/* Set this if ecmStage1 should do slow 3-multiply reduction cases. */

/* INPUTS:

Input number (3 possibilities):

1) k,b,n,c set and num_being_factored_array = NULL.  k*b^n+c is factored.
2) k,b,n,c zero and num_being_factored_array set.  num_being_factored is
   worked on using generic 3-multiply reduction
3) k,b,n,c set and num_being_factored_array set.  num_being_factored is
   worked on - it must be a factor of k*b^n+c.

A_array, B1 are required

B1_done is optional.  Use it to resume a stage 1 calculation.

x_array, z_array is the starting point.  If z_array is not given, then
z is assumed to be one.

stop_check_proc is optional

options are defined above


   OUTPUTS:

On success:

   if z_array is NULL, then x_array is set to normalized point
   else x_array, z_array is set to the unnormalized point

On factor found:

   x_array is set to the factor found

On interrupt:

   B1_done is set to the last prime below B1 that was processed.
   If z_array != NULL (preferred) then x_array and z_array are set to the
current point.  The (x,z) point is not normalized because it will
be slow for large numbers.  This is unacceptable during system shutdown.
Caller must allocate x and z arrays large enough to hold any k*b^n+c value.
   If z_array == NULL, then a normalized x is returned. Caller must allocate
x array large enough to hold any value less than num_being_factored.

*/

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
	unsigned long options);

int gwnum_ecmStage1_u64 (
	double	k,			/* K in K*B^N+C */
	unsigned long b,		/* B in K*B^N+C */
	unsigned long n,		/* N in K*B^N+C */
	signed long c,			/* C in K*B^N+C */
	uint64_t *num_being_factored_array, /* Number to factor */
	unsigned long num_being_factored_array_len,
	uint64_t B1,			/* Stage 1 bound */
	uint64_t *B1_done,		/* Stage 1 that is already done */
	uint64_t *A_array,		/* A - caller derives it from sigma */
	unsigned long A_array_len,
	uint64_t *x_array,		/* X value of point */
	unsigned long *x_array_len,
	uint64_t *z_array,		/* Z value of point */
	unsigned long *z_array_len,
	int	(*stop_check_proc)(int),/* Ptr to proc that returns TRUE */
					/* if user interrupts processing */
	unsigned long options);

/*---------------------------------------------------------------------+
|                             GWNUM INTERNALS                          |
+---------------------------------------------------------------------*/

/* This structure mimics a jmptable entry defined in the assembly code */
/* We use C code to read the entry and do lots of initialization. */
struct gwasm_jmptab {
	uint32_t max_exp;	/* Maximum exponent for this FFT len */
	uint32_t fftlen;	/* FFT length */
	float	timing;		/* Reference machine's time for a squaring */
	uint32_t flags;		/* Flags defined in mult.asm */
	void	*proc_ptr;	/* Ptr to assembly coded FFT routine */
	uint32_t mem_needed;	/* Memory needed */
	int32_t counts[8];
};

struct gwasm_alt_jmptab {	/* Used when pass 1 and pass 2 code is shared among FFT implementations */
	uint32_t max_exp;	/* Maximum exponent for this FFT len */
	uint32_t fftlen;	/* FFT length */
	float	timing;		/* Reference machine's time for a squaring */
	uint32_t flags;		/* Flags defined in mult.asm */
	void	*proc_ptr;	/* Ptr to assembly coded pass 1 FFT routine */
	void	*pass2_proc_ptr;/* Ptr to assembly coded pass 2 FFT routine */
	uint32_t mem_needed;	/* Memory needed */
	int32_t counts[8];
};

/* Structure for maintaining groups of blocks for each pass 1 thread to work on. */
/* Each thread wants to work on contiguous blocks for independent carry propagation. */
struct pass1_carry_sections {
	gwatomic change_in_progress;	/* Lock set to TRUE prior to changing start_block, last_block, next_block, section_state, etc. */
	int	section_state;		/* Various states in processing this section -- see code */
	unsigned int start_block;	/* First block in section */
	unsigned int next_block;	/* First unassigned/unprocessed block */
	unsigned int last_block;	/* Last block in section (actually first block after the section) */
	int	carry_in_blocks_finished; /* Flag indicating it is safe to propagate carries into the first blocks of this section */
	int	carry_out_section;	/* Which section might receive the carries out of this section (we'll test that section's carry_in_blocks_finished flag) */
};

/* The FFT types currently implemented in assembly code */
#define FFT_TYPE_HOME_GROWN		0
#define FFT_TYPE_RADIX_4		1
#define FFT_TYPE_RADIX_4_DELAYED	2
#define FFT_TYPE_RADIX_4_DWPN		3	/* r4delay with partial normalization */

/* The gwhandle structure containing all of gwnum's "global" data. */
struct gwhandle_struct {
	/* Variables which affect gwsetup.  These are usually set by macros above. */
	float	safety_margin;		/* Reduce maximum allowable bits per FFT data word by this amount. */
	float	polymult_safety_margin;	/* Extra safety margin required for polymult operations. */
	unsigned long minimum_fftlen;	/* Minimum fft length for gwsetup to use. */
	int	maxmulbyconst;		/* Gwsetup needs to know the maximum value the caller will use in gwsetmulbyconst. */
					/* The default value is 3, commonly used in a base-3 Fermat PRP test. */
	unsigned int num_threads;	/* Number of compute threads to use in multiply routines.  Default is obviously one. */
	char	larger_fftlen_count;	/* Force using larger FFT sizes.  This is a count of how many FFT sizes to "skip over". */
	char	force_general_mod;	/* 1 = force Montgomery general mod if possible else Barrett, 2 = force Barrett general mod */
	char	use_irrational_general_mod; /* Force using an irrational FFT when doing a Barrett general mod. */
					/* This is slower but more immune to round off errors from pathological bit patterns in the modulus. */
	char	use_large_pages;	/* Try to use 2MB/4MB pages */
	char	use_benchmarks;		/* Use benchmark data in gwnum.txt to select fastest FFT implementations */
	char	will_hyperthread;	/* Set if FFTs will use hyperthreading (affects select of fastest FFT implementation from gwnum.txt) */
	char	will_error_check;	/* Set if FFTs will error check (affects select of fastest FFT implementation from gwnum.txt) */
	char	information_only;	/* Set if doing a faster partial setup */
	char	use_spin_wait;		/* 0 = use mutex, 1 = spin wait, 2+ = ???.  Linus Torvalds hates spinning, see https://www.realworldtech.com/forum/?threadid=189711&curpostid=189723 */
					/* GWNUM doesn't use a spin lock, rather it can spin wait for an atomic counter of active threads to reach zero. */
					/* There is likely negligible difference between mutex wait and spin wait. */
	unsigned char scramble_arrays;	/* 0 = no scramble (linear addresses), 1 = light scramble (the default), 2 = full scramble, 3+ = custom (see gwnum.c code) */
					/* gwalloc_array can scramble allocated gwnums in memory.  Polymult on large polys may be faster with scrambling on. */
	int	bench_num_cores;	/* Set to expected number of cores that will FFT (affects select fastest FFT implementation) */
	int	bench_num_workers;	/* Set to expected number of workers that will FFT (affects select fastest FFT implementation) */
	int	radix_bigwords;		/* Internally used for radix conversion indicating expected number of non-zero big words.  Radix conversion has lots of */
					/* zero data and has less carry propagation issues which allows us to choose a smaller FFT length. */
	int	required_pass2_size;	/* Internally used to assure Montgomery reduction uses cyclic and negacyclic FFTs with the same pass2 size and hence */
					/* identical memory layouts */
	/* End of variables affecting gwsetup */

	double	k;			/* K in K*B^N+C */
	unsigned long b;		/* B in K*B^N+C */
	unsigned long n;		/* N in K*B^N+C */
	signed long c;			/* C in K*B^N+C */
	unsigned long FFTLEN;		/* The FFT size we are using */
	unsigned long PASS1_SIZE;	/* Number of real values FFTed in pass 1. */
	unsigned long PASS2_SIZE;	/* Number of complex values FFTed in pass 2. */
	int	cpu_flags;		/* Copy of CPU_FLAGS at time gwinit was called (just in case CPU_FLAGS changes) */
	char	ZERO_PADDED_FFT;	/* True if doing a zero pad FFT */
	char	NEGACYCLIC_FFT;		/* True if using negacyclic FFTs */
	char	RATIONAL_FFT;		/* True if bits per FFT word is integer */
	char	GENERAL_MOD;		/* True if doing general-purpose Barrett reduction */
	char	GENERAL_MMGW_MOD;	/* True if doing general-purpose Montgomery-McLaughlin-Gallot-Woltman reduction */
	char	NO_PREFETCH_FFT;	/* True if this FFT does no prefetching */
	char	IN_PLACE_FFT;		/* True if this FFT is in-place (no scratch area) */
	char	ERROR_CHECKING;		/* Set to one if FFT routines should perform roundoff error checks.  Maximum roundoff error is returned by gw_get_maxerr. */
	char	GLOBAL_MULBYCONST;	/* Internal flag used to support deprecated gwsetnormroutine c parameter.  Use GWMUL_MULBYCONST instead. */ 
	char	GLOBAL_POSTFFT;		/* Internal flag used to support deprecated gwstartnextfft routine.  Use GWMUL_STARTNEXTFFT instead. */ 
	char	POSTFFT;		/* Internal flag indicating the current multiply operation should start the forward FFT on the result */
	int	FFT_TYPE;		/* Home-grown, Radix-4, etc. */
	int	ARCH;			/* Architecture.  Which CPU type the FFT is optimized for. */
	void	(*GWPROCPTRS[15])(void*); /* Ptrs to assembly routines */
	giant	GW_MODULUS;		/* In general purpose mod case, operations are modulo this number */
	gwnum	GW_MODULUS_FFT;		/* In Barrett general purpose mod case, this is  the FFT of GW_MODULUS */
	gwnum	GW_RECIP_FFT;		/* In Barrett general purpose mod case, FFT of shifted reciprocal of GW_MODULUS */
	unsigned long GW_ZEROWORDSLOW;	/* In Barrett general purpose mod case, count of words to zero during copy step of a general purpose mod */
	unsigned long GW_GEN_MOD_MAX;	/* In Barrett general purpose mod case, maximum number of words we can safely allow in a GENERAL_MOD number */
	unsigned long GW_GEN_MOD_MAX_OFFSET; /* In Barrett general purpose mod case, offset to the GW_GEN_MOD_MAX word */
	unsigned long saved_copyz_n;	/* In Barrett general purpose mod case, used to reduce COPYZERO calculations */
	gwnum	N_Q;			/* In MMGW general purpose mod case, this is GW_MODULUS pre-FFTed for negacyclic use. */
	gwnum	Np_R;			/* In MMGW general purpose mod case, this is inverse of R=2^n-1 pre-FFTed for cyclic use. */
	gwnum	R2_4;			/* In MMGW general purpose mod case, this is inverse of R^2/4 for faster gianttogw. */
	unsigned long NUM_B_PER_SMALL_WORD; /* Number of b's in a small FFT word.  For the common case, b=2, this is the number of bits in a small word. */
	double	avg_num_b_per_word;	/* Number of base b's in each fft word */
	double	bit_length;		/* Bit length of k*b^n */
	double	fft_max_bits_per_word;	/* Maximum bits per data word that this FFT size can support */
	long	FOURKBGAPSIZE;		/* Gap between 4KB blocks in pass 2 of a two-pass FFT.  Number of cache lines in a padded block in a one-pass FFT. */
	long	PASS2GAPSIZE;		/* Gap between blocks in pass 2 */
	unsigned long PASS1_CACHE_LINES; /* Cache lines grouped together in first pass of an FFT */
	unsigned long mem_needed;	/* Memory needed for sin/cos, weights, etc. */
	unsigned long SCRATCH_SIZE;	/* Size of the pass 1 scratch area */
	float	EXTRA_BITS;		/* Number of extra bits available in FFT output because k*b^n+c is not near the FFT limit. */
					/* This is used to determine if unnormalized adds that can be safely performed. */
					/* At the FFT limit this will be set to EB_GWMUL_SAVINGS.  That is, this measures extra bits */
					/* available for multiplications, not squarings. */
	gwnum	GW_RANDOM;		/* A random number used in gwmul3_carefully. */
	gwnum	GW_RANDOM_SQUARED;	/* Cached square of the random number used in gwmul3_carefully. */
	gwnum	GW_RANDOM_FFT;		/* Cached FFT of the random number used in gwmul3_carefully. */
	gwnum	GW_FFT1;		/* The number 1 FFTed.  Sometimes need by gwmuladd4, gwmulsub4, and gwunfft. */
	gwnum	GW_ADDIN;		/* Cached gwsetaddin value when we need to emulate GWMUL_ADDINCONST. */
	gwnum	GW_POSTADDIN;		/* Cached gwsetpostmulbyconstaddin value when we need to emulate GWMUL_ADDINCONST. */
	long	emulate_addin_value;	/* When emulating GWMUL_ADDINCONST, this is a copy of the last value sent to gwsetaddin. */
	long	emulate_postaddin_value;/* When emulating GWMUL_ADDINCONST, this is a copy of the last value sent to gwsetpostmulbyconstaddin. */
	double	asm_addin_value;	/* Value to copy to asm_data->ADDIN_VALUE when GWMUL_ADDINCONST is set. */
	double	asm_postaddin_value;	/* Value to copy to asm_data->ADDIN_POSTVALUE when GWMUL_ADDINCONST is set. */
	char	FFT1_state;		/* 0 = FFT(1) needed for FMA & not yet allocated, 1 = FFT(1) needed for FMA and allocated, 2 = FFT(1) not needed for FMA. */
	char	FFT1_user_allocated;	/* TRUE if FFT(1) was allocated at user's request */
	char	polymult;		/* Set this to true if gwnums might be used by polymult library */
	char	paranoid_mul_careful;	/* Set this to TRUE if gwmul3_carefully can be called with two different source args AND the two values could be the same */
	char	GWSTRING_REP[60];	/* The gwsetup modulo number as a string. */
	int	GWERROR;		/* Set if an error is detected */
	int	mulbyconst;		/* Current mul-by-const value */
	uint64_t fft_count;		/* Count of forward and inverse FFTs */
	uint64_t read_count;		/* For memory bandwidth optimizing, a count of gwnums read (ex. a gwsquare without startnext FFT does 2 read/writes) */
	uint64_t write_count;		/* For memory bandwidth optimizing, a count of gwnums written (ex. a gwadd3 does 2 reads and one write) */
	const struct gwasm_jmptab *jmptab; /* ASM jmptable pointer */
	void	*asm_data;		/* Memory allocated for ASM global data */
	void	*dd_data;		/* Memory allocated for gwdbldbl global data */
	ghandle	gdata;			/* Structure that allows sharing giants and gwnum memory allocations */
	double	*gwnum_memory;		/* Allocated memory */
	unsigned long datasize;		/* Data size (including any cache pad lines) of a gwnum.  Does not include header before gwnum. */
	unsigned long GW_ALIGNMENT;	/* How to align allocated gwnums */
	unsigned long GW_ALIGNMENT_MOD; /* How to align allocated gwnums */
	gwnum	*gwnum_alloc;		/* Array of allocated gwnums */
	unsigned int gwnum_alloc_count; /* Count of allocated gwnums */
	unsigned int gwnum_alloc_array_size; /* Size of gwnum_alloc array */
	gwnum	*gwnum_free;		/* Array of available gwnums */
	unsigned int gwnum_free_count;	/* Count of available gwnums */
	unsigned int gwnum_max_free_count; /* Count of free gwnums that should be cached (default is 10) */
	gwarray	array_list;		/* List of arrays allocated by gwalloc_array */
	size_t	GW_BIGBUF_SIZE;		/* Size of the optional buffer */
	char	*GW_BIGBUF;		/* Optional buffer to allocate gwnums in */
	void	*large_pages_ptr;	/* Pointer to the large pages memory block we allocated. */
	void	*large_pages_gwnum;	/* Pointer to the one large pages gwnum */
	void	(*thread_callback)(int, int, void *); /* Auxiliary thread callback routine letting */
					/* the gwnum library user set auxiliary thread priority and affinity */
	void	*thread_callback_data;	/* User-supplied data to pass to the auxiliary thread callback routine */
	gwmutex alloc_lock;		/* Mutex to allow parent and clones to allocate/free gwnums in a thread-safe manner */
	gwmutex	thread_lock;		/* This mutex limits one thread at a time in critical sections. */
	gwevent	work_to_do;		/* Event (if not spin waiting) to signal auxiliary threads there is work to do */
	gwatomic alt_work_to_do;	/* Atomic alternative to work_to_do event when spin waiting */
	gwevent	all_helpers_done;	/* Event (if not spin waiting) to signal main thread that the auxiliary threads are done */
	gwatomic num_active_helpers;	/* Number of active helpers (awakened from the work_to_do event).  Is also the alternative to all_helpers_done mutex. */
	short volatile helpers_must_exit; /* Flag set to force all auxiliary threads to terminate */
	short volatile all_work_assigned; /* Flag indicating all helper thread work has been assigned (some helpers ma still be active) */
	gwevent can_carry_into;		/* This event signals pass 1 sections that the block they are waiting on to carry into may now be ready. */
	gwatomic can_carry_into_counter;/* Atomic counter to limit the resets of gwevent can_carry_into */
	int	pass1_state;		/* Mainly used to keep track of what we are doing in pass 1 of an FFT.  See */
					/* pass1_get_next_block for details.  Also, 999 means we are in pass 2 of the FFT. */
	void	*pass1_var_data;	/* pass1 variable sin/cos/premultiplier/fudge/biglit data */
	void	*adjusted_pass2_premults; /* pass2_premults pointer adjusted for the fact the first block of real FFTs have */
					/* no premultipliers */
	unsigned long biglit_data_offset; /* Offset of the big/lit data in the pass 1 variable data */
	unsigned long pass1_var_data_size; /* Used to calculate address of pass 1 premultiplier data */
	unsigned long pass2_premult_block_size; /* Used to calculate address of pass 2 premultiplier data */
	gwatomic next_block;		/* Next block for threads to process */
	unsigned long num_pass1_blocks; /* Number of data blocks in pass 1 for threads to process */
	unsigned long num_pass2_blocks; /* Number of data blocks in pass 2 for threads to process */
	unsigned long num_postfft_blocks; /* Number of data blocks that must delay forward fft because POSTFFT is set. */
	gwthread *thread_ids;		/* Array of auxiliary thread ids */
	void	**thread_allocs;	/* Array of ptrs to memory allocated for each auxiliary thread */
	struct pass1_carry_sections *pass1_carry_sections; /* Array of pass1 sections for carry propagation */
	int	pass1_carry_sections_unallocated; /* Count of auxiliary threads that have not yet been assigned block to work on */
	void	*multithread_op_data;	/* Data shared amongst add/sub/addsub/smallmul compute threads */
	uint32_t ASM_TIMERS[32];	/* Internal timers used by me to optimize code */
	int	bench_pick_nth_fft;	/* DO NOT set this variable.  Internal hack to force the FFT selection code to */
					/* pick the n-th possible implementation instead of the best one.  The prime95 */
					/* benchmarking code uses this to time every FFT implementation. */
	int	qa_pick_nth_fft;	/* DO NOT set this variable.  Internal hack to force the FFT selection code to */
					/* pick the n-th possible implementation instead of the best one.  The prime95 QA */
					/* code uses this to compare results from one FFT implementation to the (should */
					/* be identical) results of another FFT implementation. */
	int	qa_picked_nth_fft;	/* Internal hack returning which FFT implementation was selected */
	int	careful_count;		/* Count of gwsquare and gwmul3 calls to convert into gwmul3_carefully calls */
	double	ZPAD_COPY7_ADJUST[7];	/* Adjustments for copying the 7 words around the halfway point of a zero pad FFT. */
	double	ZPAD_0_6_ADJUST[7];	/* Adjustments for ZPAD0_6 in a r4dwpn FFT */
	unsigned long wpn_count;	/* Count of r4dwpn pass 1 blocks that use the same ttp/ttmp grp multipliers */
	gwatomic clone_count;		/* How many times this gwhandle has been cloned */
	gwhandle *clone_of;		/* If this is a cloned gwhandle, this points to the gwhandle that was cloned */
	gwhandle *to_radix_gwdata;	/* FFTs used in converting to base b from binary in nonbase2_gianttogw */
	gwhandle *from_radix_gwdata;	/* FFTs used in converting from base b to binary in nonbase2_gwtogiant */
	gwhandle *cyclic_gwdata;	/* Cyclic FFT used in GENERAL_MOD */
	gwhandle *negacyclic_gwdata;	/* Negacyclic FFT used in GENERAL_MOD */
	gwhandle *active_child_gwdata;	/* The cyclic or negacyclic gwdata that is currently active */
	gwhandle *parent_gwdata;	/* The parent gwdata of the cyclic and negacyclic gwdata */
};

/* A psuedo declaration for our big numbers.  The actual pointers to */
/* these big numbers are to the data array.  The 96 bytes prior to the data contain: */
/* data-4:  float containing number of unnormalized adds that have been done.  After a certain number of unnormalized adds, */
/*	    the next add must be normalized to avoid overflow errors during a multiply. */
/* data-8:  Four unused bytes. */
/* data-16: double containing the product of the two sums of the input FFT values. */
/* data-24: double containing the sum of the output FFT values.  These two */
/*	    values can be used as a sanity check when multiplying numbers. */
/*	    The two values should be "reasonably close" to one another. */
/* data-28: Flag indicating gwnum value has been partially FFTed. */
/* data-32: Allocation flags - used to free memory when done. */
/* data-88: Seven doubles (input FFT values near the halfway point when doing a zero-padded FFT). */
/* data-192: Thirteen doubles only used by the polymult library for zero-padded FFTs */
/* typedef struct { */
/*	char	pad[96];	   Used to track unnormalized add/sub and original address */
/*	double	data[512];	   The big number broken into chunks.  This array is variably sized. */
/* } *gwnum; */
#define GW_SMALL_HEADER_SIZE	32	/* Number of data bytes before a gwnum ptr when not using zero-padded FFTs */
#define GW_ZPAD_HEADER_SIZE	96	/* Number of data bytes before a gwnum ptr when using zero-padded FFTs */
#define GW_LARGE_HEADER_SIZE	192	/* Number of data bytes before a gwnum ptr when using zero-padded FFTs and polymult */
#define GW_HEADER_SIZE(h)	(!(h)->ZERO_PADDED_FFT ? GW_SMALL_HEADER_SIZE : !(h)->polymult ? GW_ZPAD_HEADER_SIZE : GW_LARGE_HEADER_SIZE)

/* Define the hidden fields before an allocated gwarray */
typedef struct {
	int	flags;		// Flags such as how array was allocated
	gwarray *prev;		// Prev pointer in doubly linked list
	gwarray next;		// Next pointer in doubly linked list
} gwarray_header;

/* Some mis-named #defines that describe the maximum Mersenne number exponent that the gwnum routines can process. */
#define MAX_PRIME	79300000L	/* Maximum number of x87 bits */
#define MAX_PRIME_SSE2	595800000L	/* SSE2 bit limit */
#define MAX_PRIME_AVX	920800000L	/* AVX bit limit */
#define MAX_PRIME_FMA3	922668300L	/* FMA3 bit limit */
#define MAX_PRIME_AVX512 1169000000L	/* AVX-512 bit limit */
#define MAX_FFTLEN	4194304L	/* 4M FFT max for x87 */
#define MAX_FFTLEN_SSE2	33554432L	/* 32M FFT max for SSE2 */
#define MAX_FFTLEN_AVX	52428800L	/* 50M FFT max for AVX */
#define MAX_FFTLEN_FMA3	52428800L	/* 50M FFT max for FMA3 */
#define MAX_FFTLEN_AVX512 67108864L	/* 64M FFT max for AVX-512 */

/* Informational routines that can be called prior to gwsetup.  Many of these routines only work for k*b^n+c FFTs. */
unsigned long gwmap_to_fftlen (double, unsigned long, unsigned long, signed long);
unsigned long gwmap_with_cpu_flags_to_fftlen (int, double, unsigned long, unsigned long, signed long);
double gwmap_to_timing (double, unsigned long, unsigned long, signed long);
unsigned long gwmap_to_memused (double, unsigned long, unsigned long, signed long);
unsigned long gwmap_fftlen_to_max_exponent (unsigned long fftlen);
unsigned long gwmap_with_cpu_flags_fftlen_to_max_exponent (int, unsigned long fftlen);
unsigned long gwmap_to_estimated_size (double, unsigned long, unsigned long, signed long);
//int gwmap_to_fft_info (gwhandle *, double, unsigned long, unsigned long, signed long); /* DEPRECATED */

/* Generate a human-readable string for k*b^n+c */
void gw_as_string(char *buf, double k, unsigned long b, unsigned long n, signed long c);

/* Other routines used (mostly) internally */
int gwinfo (gwhandle *, double, unsigned long, unsigned long, signed long);
double virtual_bits_per_word (gwhandle *);
unsigned long addr_offset (gwhandle *, unsigned long);
double *addr (gwhandle *, gwnum, unsigned long);
int get_fft_value (gwhandle *, gwnum, unsigned long, long *);
void set_fft_value (gwhandle *, gwnum, unsigned long, long);
int is_big_word (gwhandle *, unsigned long);
void bitaddr (gwhandle *, unsigned long, unsigned long *, unsigned long *);
void specialmodg (gwhandle *, giant);
#define gw_set_max_allocs(h,n)	if ((h)->gwnum_alloc==NULL) (h)->gwnum_alloc_array_size=n
void init_FFT1 (gwhandle *);

/* Use this iterator for faster incrementing through FFT data elements */
typedef struct gwiter_struct {
	gwhandle *gwdata;		/* Saved gwhandle */
	gwnum	g;			/* Saved pointer to gwnum to iterate through */
	uint32_t index;			/* Element the iterator is currently positioned on */
	intptr_t addr_offset;		/* Offset to address of the FFT data */
	bool	big_word;		/* TRUE if element is a big word */
	uint32_t switcher;		/* Combination of CPU_FLAGS, one vs. two pass, FFT type */
	uint32_t ao_values[9];		/* Nine values used to accelerate next addr_offset calculations */
	uint64_t cached_data[12];	/* Cached data to accelerate sequential access to a gwnum */
} gwiter;
void gwiter_init_zero (gwhandle *h, gwiter *iter, gwnum g);					/* Init iterator to gwnum g element zero */
void gwiter_init_write_only (gwhandle *h, gwiter *iter, gwnum g);				/* Init iterator to gwnum g element zero for writing */
void gwiter_init (gwhandle *h, gwiter *iter, gwnum g, uint32_t);				/* Init iterator to gwnum g and given element */
void gwiter_next (gwiter *iter);								/* Position to next element */
#define gwiter_index(iter)		((iter)->index)						/* Return element iterator is positioned on */
#define gwiter_addr_offset(iter)	((iter)->addr_offset)					/* Return byte offset to address of the FFT data element */
#define gwiter_addr(iter)		((double *)((char *)((iter)->g) + (iter)->addr_offset))	/* Return pointer to FFT data element */
#define gwiter_is_big_word(iter)	((iter)->big_word)					/* Return TRUE if FFT data element is a big word */
int gwiter_get_fft_value (gwiter *iter, int32_t *);						/* Return unweighted value of FFT data element */
void gwiter_set_fft_value (gwiter *iter, int32_t);						/* Weight and set FFT data element */

/* Specialized routines that let the internal giants code share the free memory pool used by gwnums. */
// void gwfree_temporarily (gwhandle *, gwnum);		DEPRECATED
// void gwrealloc_temporarily (gwhandle *, gwnum);	DEPRECATED
#define gwfree_temporarily(h,g)
#define gwrealloc_temporarily(h,g)

/* Routines to share the memory of cached free gwnums with giants code. */
/* Used by prime95 to have the giants GCD code reuse the memory used during P-1 and ECM calculations. */
void *gwgiantalloc (void *);
void gwgiantfree (void *, void *);
void gwgiantdealloc (void *);

/* When debugging gwnum and giants, I sometimes write code that "cheats" by calling a routine that is part of prime95 rather than the gwnum */
/* and giants library.  Prime95 will set this routine pointer so that gwnum code can cheat while keeping the gwnum library interface clean. */
extern void (*OutputBothRoutine)(int, const char *);

/* These routines let me time many assembly language building blocks -- used when optimizing these building blocks. */
int gwtimeit (void *);
#define get_asm_timers(g) ((uint32_t *) &(g)->ASM_TIMERS)

#ifdef __cplusplus
}
#endif

#endif
