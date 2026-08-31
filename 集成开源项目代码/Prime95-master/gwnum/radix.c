/*----------------------------------------------------------------------
| radix.c
|
| This file contains the C routines for radix conversion when required
| by gianttogw or gwtogiant.
| 
|  Copyright 2020-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

/* Include files */

#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include "cpuid.h"
#include "gwnum.h"
#include "gwutil.h"
#include "radix.h"

// IDEAS: Use libdivide library to increase BRUTE_WORDS to 4
//	Handle larger giants with brute force -- maybe 4 or 8 or 16 words?

/* Forward declarations */

int radix_gwcopy (gwhandle *src_gwdata, gwhandle *dst_gwdata, gwnum src, gwnum dst, uint64_t big_word_flags, int num_big_word_flags);
void brute_convert (gwhandle *gwdata, giant g, gwnum x, int offset, uint64_t big_word_flags);
__inline uint64_t intgcd (uint64_t a, uint64_t b) { while (b != 0) { uint64_t temp = a % b; a = b; b = temp; } return a; }

/* Internal routine to convert a giant to base != 2 gwnum FFT format. */

int nonbase2_gianttogw (	/* Returns an error code or zero for success */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	giant	g,		/* Input giant */
	gwnum	x)		/* Output gwnum */
{
	gwhandle *work_gwdata;		/* An aligned k=1 FFT to speed our radix conversion */
	gwnum	pow2_multiplier, t1, t3;
	int	i, err_code, num_chunks, fft_words_per_mult, num_big_word_flags;
	uint64_t big_word_flags;

#define BRUTE_FORCE_WORDS		2
#define giant_extract(s,off,len,d)	{if ((s)->sign <= (off)) (d)->sign = 0; \
					 else { (d)->sign = intmin ((s)->sign - (off), (len)); \
						memcpy ((d)->n, (s)->n+(off), (d)->sign * sizeof (uint32_t)); \
						while((d)->sign && (d)->n[(d)->sign-1] == 0) (d)->sign--;}}

	ASSERTG (g->sign >= 0);		/* We only handle positive numbers */

// Brute force conversion for small inputs

	if (g->sign <= BRUTE_FORCE_WORDS) {
		stackgiant(tmpg,BRUTE_FORCE_WORDS);
		gtog (g, tmpg);
		dbltogw (gwdata, 0.0, x);
		for (i = 0, big_word_flags = 0; i < 64; i++) if (is_big_word (gwdata, i)) big_word_flags |= ((uint64_t) 1) << i;
		brute_convert (gwdata, tmpg, x, 0, big_word_flags);
		return (0);
	}

// We do a new gwsetup to do our multiplications here.  In some cases, we could use the existing gwdata, but if k != 1 the
// caller's gwdata normalization code does unwanted multiplications by k.  In addition, handling the smallword / bigword differences
// would be tedious.

// The smallest FFT we can get away with must hold all our initial partial results.  This FFT size likely won't work because binary
// converted data will not fit exactly in radix-b FFT words.  Try larger and larger FFT sizes until we find an FFT that suits our needs.
// We don't restrict ourselves to rational FFTs to avoid bigword/smallword complexities, instead we require a less restrictive
// rule that every partial multiplication result uses the same bigword/smallword sequence.

	if (gwdata->to_radix_gwdata == NULL) {		// If no cached work_gwdata is available, create one
		int	exp, fftlen;			/* Exponent and fft length used to setup new_gwdata */
		int	num_pairs;
		int	b_per_mult;			/* Number of radix b that must appear in each half-pair multiply result */
		int	radix_bigwords_per_mult;	/* Expected number of bigwords in the FFT */

		// Allocate the work_gwdata
		work_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
		if (work_gwdata == NULL) goto oom;

		// Calc how big each partial result is in radix-b (with no borrow from next result)
		// This is the number of bits in a multiplication result plus one to avoid borrow divided by bits in radix b.
		b_per_mult = (int) ceil ((double) (2 * BRUTE_FORCE_WORDS * 32 + 1) / log2 (gwdata->b));
		// Calc number of chunks and pairs.  Allow for 32 extra bits so final result can't wrap-around carry.
		num_chunks = (int) ceil ((gwdata->bit_length + 32.0) / (BRUTE_FORCE_WORDS * 32));	// Total num chunks
		num_pairs = (num_chunks + 1) / 2;							// Number of pairs
		// Loop until we find a good FFT to use
		for (fftlen = 0; ; fftlen++) {
			double	words_per_mult;		/* Number of FFT words a specific FFT uses to hold a partial multiply result */
			int	option;

			// Use a negative safety margin to find a small FFT that we are unlikely to use because of possible roundoff errors.
			// Since radix conversion data has more zeroes than random data and we might find a rational FFT to use, a negative safety
			// margin is justified.  We'll work our way up from there finding the smallest usable FFT size.
			gwinit (work_gwdata);
			gwset_maxmulbyconst (work_gwdata, 1);
			gwset_safety_margin (work_gwdata, -4.00);
			gwset_minimum_fftlen (work_gwdata, fftlen);
			work_gwdata->radix_bigwords = exp = num_pairs * b_per_mult;
			err_code = gwinfo (work_gwdata, 1.0, gwdata->b, exp, -1);
			if (err_code) {			// On error, try for a small rational FFT
				ASSERTG (fftlen == 0);
				fftlen = exp - 1;	// This will find smallest possible FFT
				continue;		// Later, it will be converted to a rational FFT
			}

			fftlen = work_gwdata->FFTLEN;
			if (exp < fftlen) exp = fftlen;		// Rational FFTs have lower roundoff error
			words_per_mult = (double) b_per_mult * (double) fftlen / (double) exp;

			// To make words_per_mult an integer we have 3 options.  We can increase b_per_mult or exp to
			// reduce the words_per_mult.  Or we can increase word_per_mult as long as we can still store
			// num_pairs partial multiply results.  Or we can try a larger FFT length.

			// Option 1:  try smaller words_per_mult, increase b_per_word and exp to reduce words_per_mult to an integer
			// Option 2:  try larger words_per_mult as long as FFT can still hold num_pairs.
			// If either of these options results in the same FFT length, we have a winner
			for (option = 1; option <= 2; option++) {
				int newwpm = (int) floor (words_per_mult) + (option - 1);
				int newbpm = round_up_to_multiple_of (b_per_mult, newwpm / (int) intgcd (fftlen, newwpm));
				if (newwpm > newbpm) newwpm = newbpm;		// When base is large, use rational FFT one b per FFT word
				if (fftlen >= num_pairs * newwpm) {
					int non_zeros_per_newwpm = (int) ceil ((double) b_per_mult / ((double) newbpm / (double) newwpm));
					radix_bigwords_per_mult = ((b_per_mult - 1) % non_zeros_per_newwpm) + 1;
					gwinit (work_gwdata);
					gwset_maxmulbyconst (work_gwdata, 1);
					gwset_minimum_fftlen (work_gwdata, fftlen);
					work_gwdata->radix_bigwords = num_pairs * radix_bigwords_per_mult;
					err_code = gwinfo (work_gwdata, 1.0, gwdata->b, exp = newbpm * fftlen / newwpm, -1);
					if (err_code == 0 && fftlen >= (int) work_gwdata->FFTLEN) option = 99;	// Winner!
				}
			}
			if (option >= 99) break;

			// Option 3: Try a larger FFT length
		}
		gwinit (work_gwdata);
		gwset_maxmulbyconst (work_gwdata, 1);
		gwset_minimum_fftlen (work_gwdata, fftlen);
		work_gwdata->radix_bigwords = num_pairs * radix_bigwords_per_mult;
		if (gwdata->num_threads > 1) gwset_num_threads (work_gwdata, gwdata->num_threads);
		err_code = gwsetup (work_gwdata, 1.0, gwdata->b, exp, -1);
		if (err_code != 0) goto err;
		ASSERTG (fftlen == work_gwdata->FFTLEN);
		// Cache the work_gwdata for repeated use
		gwdata->to_radix_gwdata = work_gwdata;
	}
	work_gwdata = gwdata->to_radix_gwdata;

// Allocate temporaries for our calculations

	pow2_multiplier = gwalloc (work_gwdata);
	if (pow2_multiplier == NULL) goto oom;
	t1 = gwalloc (work_gwdata);
	if (t1 == NULL) goto oom;
	t3 = gwalloc (work_gwdata);
	if (t3 == NULL) goto oom;

// Calculate number of input chunks and number of FFT words in upper * pow2_multiplier + lower
// The number of FFT words must end on an integral number of b.

	num_chunks = divide_rounding_up (g->sign, BRUTE_FORCE_WORDS);
	fft_words_per_mult = (int) ceil (
			(2 * BRUTE_FORCE_WORDS * 32 + 1)	// Number of bits in multiplication result plus one to avoid borrow
			/ (log2 (gwdata->b) * work_gwdata->avg_num_b_per_word));	// Over bits per FFT word
	fft_words_per_mult = round_up_to_multiple_of (fft_words_per_mult, work_gwdata->FFTLEN / (int) intgcd (work_gwdata->n, work_gwdata->FFTLEN));

// Pre-calculate the bigword vs. smallword flags so that brute_convert does not need to do that repeatedly.

	ASSERTG (fft_words_per_mult <= 64);
	big_word_flags = 0;
	num_big_word_flags = fft_words_per_mult;
	for (i = 0; i < num_big_word_flags; i++) {
		if (is_big_word (work_gwdata, i)) big_word_flags |= ((uint64_t) 1) << i;
	}

// Brute force convert the first pow2_multiplier 2^(32*BRUTE_FORCE_WORDS)

	{
		stackgiant(tmpg,BRUTE_FORCE_WORDS+1);
		setone (tmpg);
		gshiftleft (32*BRUTE_FORCE_WORDS, tmpg);
		ulsubg (1, tmpg);
		dbltogw (work_gwdata, 0.0, pow2_multiplier);
		brute_convert (work_gwdata, tmpg, pow2_multiplier, 0, big_word_flags);
		*pow2_multiplier += 1.0;
	}
#ifdef GDEBUG
	gwsetnormroutine (work_gwdata,0,1,0);
#endif

// In the first round we convert from binary (BRUTE_FORCE_WORDS * 32 bits) to radix b.  We do this in lower / upper pairs that we combine
// by calculating upper * pow2_multiplier + lower.  We store the result in t1.

	int	giant_offset, gwnum_offset, num_left;

	num_chunks = (num_chunks + 1) / 2;			// This now represents the number of pairs to process
	giant_offset = 0;
	dbltogw (work_gwdata, 0.0, t3);				// We convert the lower halves to t3
	dbltogw (work_gwdata, 0.0, t1);				// We convert the upper halves to t1

	gwnum_offset = 0;
	for (num_left = num_chunks; num_left; num_left--) {
		stackgiant(tmpg,BRUTE_FORCE_WORDS);

		giant_extract (g, giant_offset, BRUTE_FORCE_WORDS, tmpg);
	        brute_convert (work_gwdata, tmpg, t3, gwnum_offset, big_word_flags);

		giant_extract (g, giant_offset + BRUTE_FORCE_WORDS, BRUTE_FORCE_WORDS, tmpg);
	        brute_convert (work_gwdata, tmpg, t1, gwnum_offset, big_word_flags);

		gwnum_offset += fft_words_per_mult;
		giant_offset += BRUTE_FORCE_WORDS * 2;
	}

	gwmul (work_gwdata, pow2_multiplier, t1);		// Multiply the upper halves by the power of two multiplier
	gwaddquick (work_gwdata, t3, t1);			// Add the lower halves to the multiplied upper halves

// Now we take our gwnum (t1) holding data converted to radix b and combine lower / upper pairs until we
// get down to just one fully radix-converted value.

	while (num_chunks > 1) {
		int	j, num_left, gwnum_offset;

		// Double the exponent of the pow2_multiplier
		gwstartnextfft (work_gwdata, 1);
		gwsquare (work_gwdata, pow2_multiplier);
		gwstartnextfft (work_gwdata, 0);

		// Process pairs of chunks.  Move FFT words from upper chunk of pair to t3.
		num_chunks = (num_chunks + 1) / 2;			// This now represents the number of pairs in t1
		dbltogw (work_gwdata, 0.0, t3);
		for (gwnum_offset = 0, num_left = num_chunks; num_left; num_left--, gwnum_offset += fft_words_per_mult * 2) {
			for (j = gwnum_offset; j < gwnum_offset + fft_words_per_mult; j++) {
				if (j + fft_words_per_mult >= (int) work_gwdata->FFTLEN) break;
				// Rational and AVX-512 FFTs and non-rdpwn (partial weights) can copy FFT data directly
				// Partial weights require using slower get and set FFT value.
				if (work_gwdata->RATIONAL_FFT || (work_gwdata->cpu_flags & CPU_AVX512F) || work_gwdata->FFT_TYPE != FFT_TYPE_RADIX_4_DWPN) {
					double	*upper_word;
					upper_word = addr (work_gwdata, t1, j + fft_words_per_mult);
					* addr (work_gwdata, t3, j) = *upper_word;
					*upper_word = 0.0;
				} else {
					long	val;
					get_fft_value (work_gwdata, t1, j + fft_words_per_mult, &val);
					set_fft_value (work_gwdata, t3, j, val);
					* addr (work_gwdata, t1, j + fft_words_per_mult) = 0.0;
				}
			}
		}

		gwmul (work_gwdata, pow2_multiplier, t3);		// Apply the power of two multiplier to the upper halves
		gwaddquick (work_gwdata, t3, t1);			// Add the multiplied upper and the lower
		fft_words_per_mult = fft_words_per_mult * 2;
	}
	ASSERTG (gw_get_maxerr(work_gwdata) < 0.43);

/* Cleanup a little */

	gwfree (work_gwdata, pow2_multiplier);
	gwfree (work_gwdata, t3);

/* Finally, write the converted result (t1) to the caller's gwdata. */

	err_code = radix_gwcopy (work_gwdata, gwdata, t1, x, big_word_flags, num_big_word_flags);
	if (err_code) goto err;

/* Finish cleanup */

	gwfree (work_gwdata, t1);

/* Return success */

	return (0);

/* Clean up and return an error */

oom:	err_code = GWERROR_MALLOC;
err:	if (work_gwdata != NULL && gwdata->to_radix_gwdata == NULL) free (work_gwdata);
	return (err_code);
}


// Brute force conversion from binary (stored in a giant) to part of a gwnum using a different radix

void brute_convert (
	gwhandle *gwdata,
	giant	g,			// Input giant (not preserved)
	gwnum	x,			// Output  gwnum
	int	offset,			// FFT word to start storing the converted giant
	uint64_t big_word_flags)	// Big word vs. small word flags
{
	int	mask1, mask2;

	mask1 = intpow ((int) gwdata->b, (int) gwdata->NUM_B_PER_SMALL_WORD);
	mask2 = gwdata->b * mask1;

/* Convert the giant to FFT format using uint64_t */

#if BRUTE_FORCE_WORDS <= 2

	if (g->sign == 0) return;
	ASSERTG (g->sign <= 2);
	{
		uint64_t g64, q;

		if (g->sign == 2) g64 = (((uint64_t) g->n[1]) << 32) + g->n[0];
		else g64 = g->n[0];
		while (g64) {
			int	mask;
			long	value;

//			mask = is_big_word (gwdata, offset) ? mask2 : mask1;
			mask = (big_word_flags & 1) ? mask2 : mask1;
			big_word_flags >>= 1;

			q = g64 / mask;				// quotient
			value = (long) (g64 - q * mask);	// value = remainder
			set_fft_value (gwdata, x, offset++, value);
			g64 = q;				// g64 = quotient
		}
	}

/* Convert the giant to FFT format */

#else
	{
		stackgiant(tmpg,BRUTE_FORCE_WORDS+1);
		stackgiant(q,BRUTE_FORCE_WORDS+1);
		while (g->sign) {
			int	mask;
			long	value;

//			mask = is_big_word (gwdata, offset) ? mask2 : mask1;
			mask = (big_word_flags & 1) ? mask2 : mask1;
			big_word_flags >>= 1;

			itog (mask, tmpg);			// tmpg = divisor
			gtog (g, q);
			divg (tmpg, q);				// q = quotient
			mulgi (&gwdata->gdata, q, tmpg);
			subg (tmpg, g);				// g = remainder
			value = (g->sign) ? g->n[0] : 0;	// value = remainder
			set_fft_value (gwdata, x, offset++, value);
			gtog (q, g);				// g = quotient
		}
	}
#endif

}

// Copy gwnum's from one gwhandle to another gwhandle using the same base

int radix_gwcopy (
	gwhandle *src_gwdata,
	gwhandle *dst_gwdata,
	gwnum	src,
	gwnum	dst,
	uint64_t big_word_flags,
	int	num_big_word_flags)
{
	int	srci, dsti, i, err_code, last_dsti;
	long	srcval, dstval, carry;
	int	srcb, dstb;
	int	radix_powers[32];

	radix_powers[0] = 1;
	radix_powers[1] = dst_gwdata->b;
	for (i = 2; i <= (int) dst_gwdata->NUM_B_PER_SMALL_WORD + 1; i++) radix_powers[i] = radix_powers[1] * radix_powers[i-1];

	srci = 0;
	srcb = 0;
	carry = 0;
	last_dsti = dst_gwdata->ZERO_PADDED_FFT ? dst_gwdata->FFTLEN / 2 + 3 : dst_gwdata->FFTLEN - 1;
	if (dst_gwdata->NUM_B_PER_SMALL_WORD == 0) while (!is_big_word (dst_gwdata, last_dsti)) last_dsti--;
	for (dsti = 0; dsti < (int) dst_gwdata->FFTLEN; dsti++) {
		int	b_we_have;

		dstb = dst_gwdata->NUM_B_PER_SMALL_WORD + !!is_big_word (dst_gwdata, dsti);
		if (dsti > last_dsti) dstb = 0;  // Needed for zero-padded FFTs
		dstval = 0;
		b_we_have = 0;

		while (b_we_have < dstb) {
			int	b_to_get, remainder;

			// Get more src data if we do not have any
			if (srcb == 0) {
				// If no more src words, we're done
				if (srci == src_gwdata->FFTLEN) break;
				// Read in next source value
				err_code = get_fft_value (src_gwdata, src, srci, &srcval);
				if (err_code) return (err_code);
				srcb = src_gwdata->NUM_B_PER_SMALL_WORD + ((big_word_flags >> (srci % num_big_word_flags)) & 1);
				srci++;
			}

			// Get part or all of source word
			b_to_get = dstb - b_we_have;
			if (b_to_get < srcb) {
				remainder = srcval % radix_powers[b_to_get];
				srcval = srcval / radix_powers[b_to_get];
				srcb -= b_to_get;
			} else {
				remainder = srcval;
				b_to_get = srcb;
				srcb = 0;
			}

			// Add the just fetched src data to the destination
			dstval += remainder * radix_powers[b_we_have];
			b_we_have += b_to_get;
		}

		// Store in balanced representation except for the last destination FFT word.
		dstval += carry;
		if (dsti == last_dsti) {			// NOTE: This occurs before the last FFT word if avg_num_b_per_word < 1 or zero-pad
			// Sometimes there is data above the last srcb -- a possible carry.
			// Get that data so we can adjust last dstval accordingly.
			ASSERTG (srcb || srci != src_gwdata->FFTLEN || dst_gwdata->ZERO_PADDED_FFT);
			if (srcb == 0 && srci != src_gwdata->FFTLEN) {
				err_code = get_fft_value (src_gwdata, src, srci, &srcval);
				if (err_code) return (err_code);
			}
			ASSERTG (srcval == 0 || srcval == 1);
			if (srcval) dstval += radix_powers[dstb];
		}
		else if (dstval < -radix_powers[dstb] / 2) {
			dstval += radix_powers[dstb];
			carry = -1;
		}
		else if (dstval > radix_powers[dstb] / 2) {
			dstval -= radix_powers[dstb];
			carry = 1;
		} else
			carry = 0;
		set_fft_value (dst_gwdata, dst, dsti, dstval);
	}

/* Return Success */

	return (0);
}


/* Internal routines to help with nonbase2_gwtogiant */


/* Grab several base b values from the gwnum source returning a uint64_t value */

struct src_data {
	int	srci;
	int	srcb;
	int	total_srcb;
	int64_t	srcval;
	int64_t	wrap_modulus;
	int64_t	wrap_val;
};

int source_extract (
	gwhandle *gwdata,
	gwnum	x,
	int	num_b_to_get,
	struct src_data *s,
	uint64_t *radix_powers,
	uint64_t *result,
	int	*wraparound_carry)		     
{
	int	b_we_have, b_to_get, err_code;
	int64_t remainder;

	*result = 0;
	for (b_we_have = 0; ; b_we_have += b_to_get) {

		// Get more src data if we do not have any and there is more to get
		if (s->srcb == 0 && s->total_srcb < (int) gwdata->n) {
			long	fftval;
			err_code = get_fft_value (gwdata, x, s->srci, &fftval);
			if (err_code) return (err_code);
			s->srcval += fftval;
			s->srcb = gwdata->NUM_B_PER_SMALL_WORD + !!is_big_word (gwdata, s->srci);
			s->total_srcb += s->srcb;
			(s->srci)++;
			// Handle last FFT word(s) special.
			if (s->total_srcb >= (int) gwdata->n) {
				int	b_above_wrap, limit;
				// When k=1, calculate the wrap around carry using just the top FFT word (see exception below).
				// Doing so ensures that the final result of nonbase2_gwtogiant does need exceed the allocated size of the giant.
				if (gwdata->k == 1.0 && !gwdata->ZERO_PADDED_FFT) {
					b_above_wrap = s->srcb;
					s->wrap_modulus = (int64_t) radix_powers[s->srcb];
					s->wrap_val = s->srcval;
					s->srcval = 0;
					s->srcb = 0;
				}
				// Wraparound carries for k > 1 are nasty!  The operation requires the last few FFT words and division by k.
				// We read all FFT words above b^n to later compute the wraparound carry.
				else {
					int	b_below_wrap;
					// split data above/below the wrap point: b^n
					b_above_wrap = s->total_srcb - gwdata->n;
					b_below_wrap = s->srcb - b_above_wrap;
					s->wrap_modulus = (int64_t) gwdata->k;
					s->wrap_val = s->srcval / (int64_t) radix_powers[b_below_wrap];
					s->srcval -= s->wrap_val * (int64_t) radix_powers[b_below_wrap];
					s->srcb = b_below_wrap;
				}
				// Read remaining FFT data.  Even in the k=1 case there can be remaining FFT data.  For example,
				// gwsmallmul of 6387^1554-11 in an FFT length 4608 outputs data to the top two "empty" FFT words.
				limit = (gwdata->ZERO_PADDED_FFT) ? gwdata->FFTLEN / 2 + 4 : gwdata->FFTLEN;
				while (s->srci < limit) {
					err_code = get_fft_value (gwdata, x, s->srci, &fftval);
					if (err_code) return (err_code);
					s->wrap_val += fftval * (int64_t) radix_powers[b_above_wrap];
					b_above_wrap += gwdata->NUM_B_PER_SMALL_WORD + !!is_big_word (gwdata, s->srci);
					(s->srci)++;
				}
			}
		}

		// If we still don't have source data and we've read all the FFT data, then work on the wraparound carry
		// and its remainder.  Apply the wrap modulus.
		if (s->srcb == 0 && s->total_srcb >= (int) gwdata->n) {
			s->srcval += s->wrap_val;
			if (s->wrap_modulus == 1) {			// This can happen in sparse FFTs and zero-padded FFTs
				*wraparound_carry = (int) s->srcval;
				s->srcval = 0;
			}
			else if (s->srcval >= 0) {
				*wraparound_carry = (int) (s->srcval / s->wrap_modulus);
				s->srcval -= *wraparound_carry * s->wrap_modulus;
			}
			else {
				*wraparound_carry = (int) (s->srcval / s->wrap_modulus - 1);
				s->srcval -= *wraparound_carry * s->wrap_modulus;
			}
			s->srcb = 9999;
		}

		// It seems weird to have our loop exit here.  Why get more source words when we don't need any more for our result?
		// The answer came from QA'ing 2932^1870+11.  Prior to moving the exit here, with k=1 the last caller got his data
		// from the last FFT value.  The wraparound_carry calculation above never happened.
		if (b_we_have == num_b_to_get) break;

		// Get part or all of source word
		b_to_get = intmin (s->srcb, num_b_to_get - b_we_have);
		while ((int64_t) radix_powers[b_to_get] <= 0) b_to_get--;	// Make sure radix power can be successfully changed to int64_t
		remainder = s->srcval % (int64_t) radix_powers[b_to_get];
		s->srcval = s->srcval / (int64_t) radix_powers[b_to_get];
		s->srcb -= b_to_get;
		if (remainder < 0) {
			remainder += (int64_t) radix_powers[b_to_get];
			s->srcval -= 1;
		}

		// Add the just fetched src data to the destination
		*result += remainder * (int64_t) radix_powers[b_we_have];
	}

/* Return Success */

	return (0);
}


/* Output up to 64-bits to part of a gwnum specified by gwnum_offset and num_fft_words */

__inline void brute_output64 (
	gwhandle *gwdata,
	gwnum	x,
	int	gwnum_offset,
	int	num_fft_words,
	uint64_t value,
	uint64_t big_word_flags)
{
	int	i;

	for (i = 0; i < num_fft_words; i++) {
		int	num_bits = gwdata->NUM_B_PER_SMALL_WORD + (big_word_flags & 1);
		set_fft_value (gwdata, x, gwnum_offset + i, value & ((1 << num_bits) - 1));
		value >>= num_bits;
		big_word_flags >>= 1;
	}
}


/* Internal routine to convert a base != 2 gwnum FFT format to giant. */

int nonbase2_gwtogiant (	/* Returns an error code or zero for success */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x,		/* Input gwnum */
	giant	g)		/* Output giant */
{
	gwhandle *work_gwdata;		/* A base-2 FFT to speed our radix conversion */
	gwnum	powb_multiplier, t1, t3;
	int	i, b_per_uint64, err_code, num_chunks, fft_words_per_mult, num_big_word_flags;
	uint64_t big_word_flags;

	// Calc how the number of b's we can safely put in a uint64_t
	b_per_uint64 = (int) floor (64.0 / log2 (gwdata->b));
	// Calc number of chunks to convert.  Allow for 32 extra bits so final result can't wrap-around carry.
	num_chunks = (int) ceil (((double) gwdata->n + log2(gwdata->k) + 32.0) / (double) b_per_uint64);

// We do a new gwsetup to do our multiplications.  The smallest FFT we can get away must hold all our initial partial results.
// This FFT size likely won't work.  Try larger and larger FFT sizes until we find an FFT that suits our needs.
// We don't restrict ourselves to rational FFTs to avoid bigword/smallword complexities, instead we require a less restrictive
// rule that every partial multiplication result uses the same bigword/smallword sequence.

	if (gwdata->from_radix_gwdata == NULL) {	// If no cached work_gwdata is available, create one
		int	exp, fftlen;			/* Exponent and fft length used to setup new_gwdata */
		int	num_pairs;
		int	bits_per_mult;			/* Bits in each half-pair multiply result */

		// Allocate the work_gwdata
		work_gwdata = (gwhandle *) malloc (sizeof (gwhandle));
		if (work_gwdata == NULL) goto oom;

		// Calc the number of bits in a multiplication result plus one to avoid borrows during normalization.
		// Remember that the first FFT word could be larger due to the wraparound carry.  This shouldn't affect this
		// calculation unless gwdata->b is a power of 2 -- we add an extra 0.1 to handle this case.
		bits_per_mult = (int) ceil ((double) (b_per_uint64 * 2) * log2 (gwdata->b) + 0.1) + 1;
		// Calc number of pairs
		num_pairs = (num_chunks + 1) / 2;						// Number of pairs
		// Loop until we find a good FFT to use
		for (fftlen = 0; ; fftlen++) {
			double	words_per_mult;		/* Number of FFT words a specific FFT uses to hold a partial multiply result */
			int	option;

			gwinit (work_gwdata);
			gwset_maxmulbyconst (work_gwdata, 1);
			gwset_minimum_fftlen (work_gwdata, fftlen);
			err_code = gwinfo (work_gwdata, 1.0, 2, exp = num_pairs * bits_per_mult, -1);
			if (err_code) goto err;		// On error, try for a small rational FFT

			fftlen = work_gwdata->FFTLEN;
			words_per_mult = (double) bits_per_mult * (double) fftlen / (double) exp;

			// Do we have an FFT we can use?  FFT words per mult must be an integer so that every partial multiplication
			// result has the same bigword/smallword pattern.  And the FFT must be large enough to hold all our pairs.
			if (words_per_mult == ceil (words_per_mult) && fftlen >= num_pairs * (int) words_per_mult) break;

			// To make words_per_mult an integer we have 3 options.  We can increase b_per_mult or exp to
			// reduce the words_per_mult.  Or we can increase word_per_mult as long as we can still store
			// num_pairs partial multiply results.  Or we can try a larger FFT length.

			// Option 1:  try smaller words_per_mult, increase b_per_word and exp to reduce words_per_mult to an integer
			// Option 2:  try larger words_per_mult as long as FFT can still hold num_pairs.
			// If either of these options results in the same FFT length, we have a winner
			for (option = 1; option <= 2; option++) {
				int newwpm = (int) floor (words_per_mult) + (option - 1);
				int newbpm = round_up_to_multiple_of (bits_per_mult, newwpm / (int) intgcd (fftlen, newwpm));
				if (fftlen >= num_pairs * newwpm) {
					gwinit (work_gwdata);
					gwset_maxmulbyconst (work_gwdata, 1);
					gwset_minimum_fftlen (work_gwdata, fftlen);
					err_code = gwinfo (work_gwdata, 1.0, 2, exp = newbpm * fftlen / newwpm, -1);
					if (err_code == 0 && fftlen >= (int) work_gwdata->FFTLEN) option = 99;	// Winner!
				}
			}
			if (option >= 99) break;

			// Option 3: Try a larger FFT length
		}
		gwinit (work_gwdata);
		gwset_maxmulbyconst (work_gwdata, 1);
		gwset_minimum_fftlen (work_gwdata, fftlen);
		if (gwdata->num_threads > 1) gwset_num_threads (work_gwdata, gwdata->num_threads);
		err_code = gwsetup (work_gwdata, 1.0, 2, exp, -1);
		if (err_code) goto err;
		ASSERTG (fftlen == work_gwdata->FFTLEN);
		// Cache the work_gwdata for repeated use
		gwdata->from_radix_gwdata = work_gwdata;
	}
	work_gwdata = gwdata->from_radix_gwdata;

// Allocate temporaries for our calculations

	powb_multiplier = gwalloc (work_gwdata);
	if (powb_multiplier == NULL) goto oom;
	t1 = gwalloc (work_gwdata);
	if (t1 == NULL) goto oom;
	t3 = gwalloc (work_gwdata);
	if (t3 == NULL) goto oom;

// Calculate number of FFT words in upper * powb_multiplier + lower
// The number of FFT words must end on an integral number.

	fft_words_per_mult = (int) ceil ((((double) (b_per_uint64 * 2) * log2 (gwdata->b) + 0.1) + 1.0)	// Bits in multiplication result plus one to avoid borrow
					/ work_gwdata->avg_num_b_per_word);				// Over bits per FFT word
	fft_words_per_mult = round_up_to_multiple_of (fft_words_per_mult, work_gwdata->FFTLEN / (int) intgcd (work_gwdata->n, work_gwdata->FFTLEN));

// Pre-calculate the bigword vs. smallword flags so that chunk conversion does not need to do that repeatedly.

	ASSERTG (fft_words_per_mult <= 64);
	big_word_flags = 0;
	num_big_word_flags = fft_words_per_mult;
	for (i = 0; i < num_big_word_flags; i++) {
		if (is_big_word (work_gwdata, i)) big_word_flags |= ((uint64_t) 1) << i;
	}

// Calc the first powb_multiplier b^b_per_uint64

	{
		stackgiant (tmpg, 3);				// If b is a power of 2 then tmpg may need to hold 2^64 which requires three 32-bit words
		ultog (gwdata->b, tmpg);
		power (tmpg, b_per_uint64);
		gianttogw (work_gwdata, tmpg, powb_multiplier);
	}
#ifdef GDEBUG
	gwsetnormroutine (work_gwdata,0,1,0);
#endif

// In the first round we convert from radix gwdata->b to binary.  We do this in lower / upper pairs that we combine
// by calculating upper * powb_multiplier + lower.  We store the result in t1.

	{
		int	gwnum_offset, num_left, wraparound_carry;
		uint64_t radix_powers[64];
		struct src_data s;

		// Precompute powers of radix gwdata->b
		radix_powers[0] = 1;
		radix_powers[1] = gwdata->b;
		for (i = 2; i <= b_per_uint64; i++) radix_powers[i] = radix_powers[1] * radix_powers[i-1];

		num_chunks = (num_chunks + 1) / 2;			// This now represents the number of pairs to process
		dbltogw (work_gwdata, 0.0, t3);				// We convert the lower halves to t3
		dbltogw (work_gwdata, 0.0, t1);				// We convert the upper halves to t1

		// Init source data pointers
		memset (&s, 0, sizeof (s));

		// Convert from radix to binary in 64-bit chunks.  Output lower half of a pair to t3, upper half to t1.
		gwnum_offset = 0;
		for (num_left = num_chunks; num_left; num_left--) {
			uint64_t value;

			err_code = source_extract (gwdata, x, b_per_uint64, &s, radix_powers, &value, &wraparound_carry);
			if (err_code) goto err;
			brute_output64 (work_gwdata, t3, gwnum_offset, (fft_words_per_mult + 1) / 2, value, big_word_flags);

			err_code = source_extract (gwdata, x, b_per_uint64, &s, radix_powers, &value, &wraparound_carry);
			if (err_code) goto err;
			brute_output64 (work_gwdata, t1, gwnum_offset, (fft_words_per_mult + 1) / 2, value, big_word_flags);

			gwnum_offset += fft_words_per_mult;
		}
		*t3 += (double) wraparound_carry * (double) -gwdata->c; // Process the possible carry out of the top source FFT word

		gwmul (work_gwdata, powb_multiplier, t1);		// Multiply the upper halves by the power of b multiplier
		gwaddquick (work_gwdata, t3, t1);			// Add the lower halves to the multiplied upper halves
	}

// Now we take our gwnum (t1) holding data converted from radix b and combine lower / upper pairs until we
// get down to just one fully radix-converted value.

	while (num_chunks > 1) {
		int	j, num_left, gwnum_offset;

		// Double the exponent of the powb_multiplier
		gwstartnextfft (work_gwdata, 1);
		gwsquare (work_gwdata, powb_multiplier);
		gwstartnextfft (work_gwdata, 0);

		// Process pairs of chunks.  Move FFT words from upper chunk of pair to t3.
		num_chunks = (num_chunks + 1) / 2;			// This now represents the number of pairs in t1
		dbltogw (work_gwdata, 0.0, t3);
		for (gwnum_offset = 0, num_left = num_chunks; num_left; num_left--, gwnum_offset += fft_words_per_mult * 2) {
			for (j = gwnum_offset; j < gwnum_offset + fft_words_per_mult; j++) {
				if (j + fft_words_per_mult >= (int) work_gwdata->FFTLEN) break;
				// Rational and AVX-512 FFTs and non-rdpwn (partial weights) can copy FFT data directly
				// Partial weights require using slower get and set FFT value.
				if (work_gwdata->RATIONAL_FFT || (work_gwdata->cpu_flags & CPU_AVX512F) || work_gwdata->FFT_TYPE != FFT_TYPE_RADIX_4_DWPN) {
					double	*upper_word;
					upper_word = addr (work_gwdata, t1, j + fft_words_per_mult);
					* addr (work_gwdata, t3, j) = *upper_word;
					*upper_word = 0.0;
				} else {
					long	val;
					get_fft_value (work_gwdata, t1, j + fft_words_per_mult, &val);
					set_fft_value (work_gwdata, t3, j, val);
					* addr (work_gwdata, t1, j + fft_words_per_mult) = 0.0;
				}
			}
		}

		gwmul (work_gwdata, powb_multiplier, t3);		// Apply the power of b multiplier to the upper halves
		gwaddquick (work_gwdata, t3, t1);			// Add the multiplied upper and the lower
		fft_words_per_mult = fft_words_per_mult * 2;
	}
	ASSERTG (gw_get_maxerr(work_gwdata) < 0.43);

/* Cleanup a little */

	gwfree (work_gwdata, powb_multiplier);
	gwfree (work_gwdata, t3);

/* Finally, write the converted result (t1) to a giant.  We cannot simply call gwtogiant as that may write to too many words in the */
/* giant (work_gwdata->bit_length may be significantly larger than gwdata->bit_length).  So, we copied much of the k=1, b=2, c=-1 gwtogiant code which */
/* also lets us perform a few optimizations. */

	{
		gwiter	iter;
		int32_t	val;
		int64_t accum;
		int	bits, accumbits;
		unsigned long limit;
		uint32_t *outptr;

/* Collect bits until we have all of them */

		accum = 0;
		accumbits = 0;
		outptr = g->n;
		limit = divide_rounding_up ((int) ceil (gwdata->bit_length), 32) + 1;
		for (gwiter_init_zero (work_gwdata, &iter, t1); ; ) {
			if (gwiter_index (&iter) < (int) work_gwdata->FFTLEN) {
				err_code = gwiter_get_fft_value (&iter, &val);
				if (err_code) goto err;
				bits = work_gwdata->NUM_B_PER_SMALL_WORD;
				if (gwiter_is_big_word (&iter)) bits++;
				accum += ((int64_t) val) << accumbits;
				accumbits += bits;
				gwiter_next (&iter);
				if (accumbits < 32) continue;
			}
			*outptr++ = (uint32_t) accum;
			accum >>= 32;
			accumbits -= 32;
			if (outptr == g->n + limit) break;
		}
		ASSERTG (accum == 0 || accum == -1);

/* Set the length */

		g->sign = (long) (outptr - g->n);
		while (g->sign && g->n[g->sign-1] == 0) g->sign--;

/* Divide the upper bits by k, leave the remainder in the upper bits and multiply the quotient by c and subtract that from the lower bits. */

		stackgiant(upper,5);					// Upper bits shouldn't be much more than k^2 (100 bits)
		gtogshiftrightsplit (work_gwdata->n, g, upper, accum);	// Split v at bit n into two giants optionally negating upper bits
		if (!isZero (upper)) addg (upper, g);			// Lower bits minus upper bits * c
	}

/* Finish cleanup */

	gwfree (work_gwdata, t1);

/* Return success */

	return (0);

/* Clean up and return an error */

oom:	err_code = GWERROR_MALLOC;
err:	if (work_gwdata != NULL && gwdata->from_radix_gwdata == NULL) free (work_gwdata);
	return (err_code);
}
