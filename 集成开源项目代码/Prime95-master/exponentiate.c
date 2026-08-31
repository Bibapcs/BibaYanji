/*----------------------------------------------------------------------
| Copyright 2020-2022 Mersenne Research, Inc.  All rights reserved
|
| Auxiliary routines to exponentiate gwnums
+---------------------------------------------------------------------*/

/* Includes */

#include "common.h"		// Included in all GIMPS sources
#include "exponentiate.h"

// Macro copied from commonb.h

#define cvt_mem_to_gwnums(g,m) ((unsigned long)(((double)(m)*1048576.0-1000000.0-(double)gwmemused(g))/(double)gwnum_size(g)))

// This version is a simple left-to-right powering

// Raise a gwnum to a power
void simple_exponentiate (gwhandle *gwdata, gwnum x, uint64_t power)
{
	uint64_t current_bit;
	int	first_squaring;
	gwnum	scratch;		// Scratch gwnum for code to use

	// Handle simple cases
	if (power == 0) { dbltogw (gwdata, 1.0, x); return; }
	if (power == 1) return;

	scratch = gwalloc (gwdata);
//BUG	if (scratch == NULL) goto oom;

	// Find the topmost bit in power
	for (current_bit = 0x8000000000000000ULL; power < current_bit; current_bit >>= 1);

	// Exponentiate
	first_squaring = 1;
	for (current_bit >>= 1; current_bit; current_bit >>= 1) {
		if (first_squaring) {
			gwfft (gwdata, x, scratch);
			gwmul3 (gwdata, scratch, scratch, x, (current_bit != 1 || (power & 1) == 0) ? GWMUL_STARTNEXTFFT : 0);
			first_squaring = 0;
		} else
			gwsquare2 (gwdata, x, x, (current_bit != 1 || (power & 1) == 0) ? GWMUL_STARTNEXTFFT : 0);
		if (power & current_bit)
			gwmul3 (gwdata, scratch, x, x, current_bit != 1 ? GWMUL_STARTNEXTFFT : 0);
	}

	// Free allocated memory
	gwfree (gwdata, scratch);
}


// Utility routine to help with bit testing

#define is_bit_set(array,bitnum)	((array)[(bitnum)>>6] & (1ULL << ((bitnum) & 63)))

// Raise a gwnum to a power.  This version uses a 3-bit windowing scheme to reduce multiplies a little

void exponentiate_window3 (gwhandle *gwdata, gwnum x, uint64_t *power, int arraylen)
{
	int	current_bit, top3, first_squaring_done;
	gwnum	x1, x3, x5, x7;		// Scratch gwnums for code to use
	int	x7_is_really_x2;

	// Allocate space for temporaries
	x1 = gwalloc (gwdata);
	x3 = gwalloc (gwdata);
	x5 = gwalloc (gwdata);
	x7 = gwalloc (gwdata);
//BUG	if (x1 == NULL || x3 == NULL || x5 == NULL || x7 == NULL) goto oom;

	// Pre-compute x^1, x^3, x^5.  We delay computing x^7 until we are sure we will need it.
	gwfft (gwdata, x, x1);
	gwmul3 (gwdata, x1, x1, x7, GWMUL_STARTNEXTFFT); x7_is_really_x2 = 1;
	gwmul3 (gwdata, x7, x1, x3, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
	gwmul3 (gwdata, x7, x3, x5, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);

	// Find the topmost bit in power
	current_bit = arraylen * 64 - 1;
	while (!is_bit_set (power, current_bit)) current_bit--;

	// Special case the first 3 bits of the exponent
	// 100:  Normal would be 1,2,4,8.  We do 1,2,3,5,8		Normal is better only if x3,x5 never used
	// 101:  Normal would be 1,2,4,5.  We do 1,2,3,5
	// 110:  Normal would be 1,2,3,6,12.  We do 1,2,3,5,7,12	Normal is better only if x5,x7 never used
	// 111:  Normal would be 1,2,3,6,7.  We do 1,2,3,5,7
	top3 = 4;
	if (is_bit_set (power, current_bit - 1)) top3 += 2;
	if (is_bit_set (power, current_bit - 2)) top3 += 1;
	if (top3 == 4) gwmul3 (gwdata, x3, x5, x, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	if (top3 == 5) gwmul3 (gwdata, x5, x5, x, GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	if (top3 == 6) {
		gwmul3 (gwdata, x7, x5, x7, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT); x7_is_really_x2 = 0;
		gwmul3 (gwdata, x5, x7, x, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	}
	if (top3 == 7) {
		gwmul3 (gwdata, x7, x5, x7, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT); x7_is_really_x2 = 0;
		gwmul3 (gwdata, x7, x7, x, GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	}
	first_squaring_done = 1;	// Set flag indicating we've already done the first squaring needed in exponentiate loop

	// Sliding window exponentiate
	for (current_bit -= 3; current_bit >= 0; current_bit--) {
		if (first_squaring_done) first_squaring_done = 0;
		else gwsquare2 (gwdata, x, x, current_bit || is_bit_set (power, 0) ? GWMUL_STARTNEXTFFT : 0);
		if (!is_bit_set (power, current_bit)) continue;
		// We have a window starting with a set bit, see if we want to apply x^1, x^3, x^5, or x^7
		if (current_bit >= 2 && is_bit_set (power, current_bit - 2)) {
			gwsquare2 (gwdata, x, x, GWMUL_STARTNEXTFFT);
			gwsquare2 (gwdata, x, x, GWMUL_STARTNEXTFFT);
			if (is_bit_set (power, current_bit - 1)) {	// Apply x^7
				if (x7_is_really_x2) { gwmul3 (gwdata, x7, x5, x7, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT); x7_is_really_x2 = 0; }
				gwmul3 (gwdata, x7, x, x, GWMUL_FFT_S1 | (current_bit != 2 ? GWMUL_STARTNEXTFFT : 0));
			} else {					// Apply x^5
				gwmul3 (gwdata, x5, x, x, GWMUL_FFT_S1 | (current_bit != 2 ? GWMUL_STARTNEXTFFT : 0));
			}
			current_bit -= 2;
		} else {
			if (current_bit >= 1 && is_bit_set (power, current_bit - 1)) {	// Apply x^3
				gwsquare2 (gwdata, x, x, GWMUL_STARTNEXTFFT);
				gwmul3 (gwdata, x3, x, x, current_bit != 1 ? GWMUL_STARTNEXTFFT : 0);
				current_bit -= 1;
			} else {				// Apply x^1
				gwmul3 (gwdata, x1, x, x, current_bit != 1 ? GWMUL_STARTNEXTFFT : 0);
			}
		}
	}

	// Free allocated memory
	gwfree (gwdata, x1);
	gwfree (gwdata, x3);
	gwfree (gwdata, x5);
	gwfree (gwdata, x7);
}

// Raise a gwnum to a power.  This version uses a 5-bit windowing scheme to reduce multiplies even more.

void exponentiate_windowed (gwhandle *gwdata, gwnum x, uint64_t *power, int bitlen, int num_temps)
{
	gwnum	*xm;			// Scratch gwnums for code to use
	int	max_mult, current_bit, topbits;
	bool	first_squaring_done;

	// Allocate space for array of temporaries
	max_mult = num_temps * 2 - 1;
	xm = (gwnum *) malloc ((max_mult + 1) * sizeof (gwnum));

	// Allocate space for temporaries
	for (int i = 1; i <= max_mult; i += 2) {
		xm[i] = gwalloc (gwdata);
		if (xm[i] == NULL) {
			max_mult = i - 2;
			break;
		}
	}

	// Pre-compute x^1, x^3, x^5, ... x^max_mult
	gwfft (gwdata, x, xm[1]);
	gwmul3 (gwdata, xm[1], xm[1], xm[max_mult], GWMUL_STARTNEXTFFT);
	for (int i = 3; i <= max_mult; i += 2) gwmul3 (gwdata, xm[max_mult], xm[i-2], xm[i], GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);

	// Special case the top bits of the exponent
	for (topbits = 1, current_bit = bitlen - 2; topbits + topbits + 1 <= max_mult; current_bit--)
		topbits = topbits + topbits + (is_bit_set (power, current_bit) ? 1 : 0);
	if ((topbits & 1) == 0)
		gwmul3 (gwdata, xm[topbits-1], xm[topbits+1], x, GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	else
		gwmul3 (gwdata, xm[topbits], xm[topbits], x, GWMUL_FFT_S1 | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	first_squaring_done = 1;	// Set flag indicating we've already done the first squaring needed in exponentiate loop

	// Sliding window exponentiate
	while (current_bit >= 0) {
		int	mult;
		if (first_squaring_done) first_squaring_done = 0;
		else gwsquare2 (gwdata, x, x, current_bit || is_bit_set (power, 0) ? GWMUL_STARTNEXTFFT : 0);
		if (!is_bit_set (power, current_bit)) { current_bit--; continue; }
		// We have a window starting with a set bit, see if we want to apply x^1, x^3, ... x^max_bits
		for (mult = 1, current_bit--; current_bit >= 0 && mult + mult + 1 <= max_mult; current_bit--)
			mult = mult + mult + (is_bit_set (power, current_bit) ? 1 : 0);
		while ((mult & 1) == 0) mult >>= 1, current_bit++;
		for (int temp = mult; temp > 1; temp >>= 1) gwsquare2 (gwdata, x, x, GWMUL_STARTNEXTFFT);
		gwmul3 (gwdata, xm[mult], x, x, GWMUL_FFT_S1 | (current_bit != 0 ? GWMUL_STARTNEXTFFT : 0));
	}

	// Free allocated memory
	for (int i = 1; i <= max_mult; i += 2) gwfree (gwdata, xm[i]);
	free (xm);
}

// Internal routine to call an exponentiate routine that uses the appropriate window size

void exponentiate_array (gwhandle *gwdata, gwnum x, uint64_t *power, int arraylen, int num_temps)
{
	// Handle simple cases.  In theory, window size 3 breaks even with simple exponentiation at around 9 bits.
	while (arraylen >= 1 && power[arraylen-1] == 0) arraylen--;

	// Use legacy code for small exponentiations
	if (arraylen <= 1) {
		if (arraylen == 0) simple_exponentiate (gwdata, x, 0);
		else if (power[0] <= 256 || num_temps < 4) simple_exponentiate (gwdata, x, power[0]);
		else exponentiate_window3 (gwdata, x, power, arraylen);
	}
	// Handle case where caller suggests 4 temps or less.  The minimum window size of 3 uses 4 temps.
	else if (num_temps <= 4) {
		//GW:  in num_temps < 4, we should write a simple_exponentiate that takes an array of uint64_t as input.
		exponentiate_window3 (gwdata, x, power, arraylen);
	}
	// This formula is the expected number of squarings/multiplies for a window size of 3 (4 gwnum temps):  5 + (len-5) + (len-4)/4
	// Generalize this formula to "temps+1 + (len-(log2(temps)+2)) + (len-(log2(temps)+1))/(log2(temps)+2)" and use a binary
	// search to find the best number of temps.
	else {
		int	bitlen;
		struct {
			int	temps;		/* Number of temps to use */
			double	cost;		/* Expected cost in squarings/multiplies */
		} best[3], midpoint;
		#define expcost(x,t)	x.temps = t, x.cost = (t+1) + (bitlen-(log2(t)+2)) + (bitlen-(log2(t)+1))/(log2(t)+2)

		// Compute the bitlength of the power
		for (bitlen = arraylen * 64; !is_bit_set (power, bitlen-1); bitlen--);

		// Three random starting points for binary searching
		expcost (best[0], 4);
		expcost (best[1], 250);
		expcost (best[2], 500);

		// Handle case where midpoint is worse than the start point.  Search code requires best[1] is better than best[0] and best[2].
		while (best[0].cost < best[1].cost) {
			best[2] = best[1];
			expcost (best[1], (best[0].temps + best[2].temps) / 2);
		}
		// Handle case where midpoint is worse than the end point.  Search code requires best[1] is better than best[0] and best[2].
		while (best[1].cost > best[2].cost) {
			best[0] = best[1];
			best[1] = best[2];
			expcost (best[2], best[1].temps * 2);
		}

		// Find the best number of temps
		while (best[0].temps + 2 != best[2].temps) {
			// Work on the bigger of the lower section and upper section
			if (best[1].temps - best[0].temps > best[2].temps - best[1].temps) {	// Work on lower section
				expcost (midpoint, (best[0].temps + best[1].temps) / 2);
				if (midpoint.cost < best[1].cost) {				// Make middle the new end point
					best[2] = best[1];
					best[1] = midpoint;
				} else {							// Create new start point
					best[0] = midpoint;
				}
			} else {								// Work on upper section
				expcost (midpoint, (best[1].temps + best[2].temps) / 2);
				if (midpoint.cost < best[1].cost) {				// Make middle the new start point
					best[0] = best[1];
					best[1] = midpoint;
				} else {							// Create new end point
					best[2] = midpoint;
				}
			}
		}

		// Exponentiate using optimal number of temps
		exponentiate_windowed (gwdata, x, power, bitlen, _intmin (num_temps, best[1].temps));
	}
}

// Raise a gwnum to a power
void exponentiate (gwhandle *gwdata, gwnum x, uint64_t power)
{
	exponentiate_array (gwdata, x, &power, 1, 16);
}

// Raise a gwnum to a mpz power (assume 1GB memory available for temps
void exponentiate_mpz (gwhandle *gwdata, gwnum x, mpz_t power)
{
	exponentiate_mpz_limited_temps (gwdata, x, power, cvt_mem_to_gwnums (gwdata, 1024));
}

// Raise a gwnum to a mpz power using a maximum number of temporaries
void exponentiate_mpz_limited_temps (gwhandle *gwdata, gwnum x, mpz_t power, int num_temps)
{
	uint64_t *array;
	size_t	len;

	array = (uint64_t *) malloc (divide_rounding_up (mpz_sizeinbase (power, 2), 64) * sizeof (uint64_t));
	mpz_export (array, &len, -1, sizeof (uint64_t), 0, 0, power);
	exponentiate_array (gwdata, x, array, (int) len, num_temps);
	free (array);
}

