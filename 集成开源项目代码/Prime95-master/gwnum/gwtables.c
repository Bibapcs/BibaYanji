/*----------------------------------------------------------------------
| gwtables.c
|
| This file contains the C routines to build sin/cos and weights tables
| that the FFT assembly code needs.
| 
|  Copyright 2002-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

/* Include files */

#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include "gwnum.h"
#include "gwtables.h"
#include "gwdbldbl.h"

#define USE_WPN4
#define USE_REDUCED_SINCOS_FFTS

#define round_to_cache_line(p)	(void *) (((intptr_t)(p) + 63) & ~63)
#define bitset(a,i)		{ ((char *)a)[(i) >> 3] |= (1 << ((i) & 7)); }

/* Find the power of two greater than or equal to N. */

unsigned long pow_two_above_or_equal (
	unsigned long n)
{
	unsigned long result;

	result = 1;
	for (n = n - 1; n; n = n >> 1) result = result << 1;
	return (result);
}


/*************************************************************/
/*                    AVX-512 FFT tables                     */
/*************************************************************/

/* Helper routine for two pass AVX-512 build routines */

static __inline int zr4dwpn_delay_count (gwhandle *gwdata)
{

/* Determine number of delay groups.  In a standard radix-8 FFT, there is only one sin/cos */
/* group in the last pass 1 level.  We reduce our memory usage by using a fixed sin/cos */
/* table in the first FFT levels and having multiple groups of sin/cos data in the last pass 1 level. */
/* I call these groups of sin/cos data in the last pass 1 level "delay groups". */

//	if (gwdata->PASS1_SIZE == 80) return (5);	// BUG - delay count of 5 is not working yet and may never work
	if (gwdata->PASS1_SIZE == 192 || gwdata->PASS1_SIZE == 960 || gwdata->PASS1_SIZE == 1152 || gwdata->PASS1_SIZE == 1344 ||
	    gwdata->PASS1_SIZE == 1920 || gwdata->PASS1_SIZE == 2304 || gwdata->PASS1_SIZE == 3072) return (12);  // BUG Could do 1536 this way instead -- will use less memory
	return (8);		// BUG:  applies to pass1 size = 128, 640, 768, 896, 1024, 1280, 1536, 2048
}

/* This routine builds the sin/cos table used in pass 1 by the radix-8 DJB */
/* FFT with delayed sin/cos multiplies and with partial normalization. */

double *zr4dwpn_build_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned long pass1_size, pass1_increment, delay_count;
	unsigned long group, i, j, k, N, temp, upper_avx512_word;

/* Special code to initialize one-pass FFTs */

	if (gwdata->PASS1_SIZE == 0) {

/* Output the 8 column multipliers and 8 inverse column multipliers used in real pass 1 wrapper routines. */
/* We used to apply the 2/FFTLEN correction in the group multiplier rather than the inverse column multiplier saving one clock in the unfft wrapper. */
/* However, this corrupted FFT(1) which is bad for gwmulmuladd and polymult. */

		if (!gwdata->NEGACYCLIC_FFT) {
			double *weights, *inverse_weights;
			weights = table;
			inverse_weights = weights + 8;
			table = inverse_weights + 8;
			for (i = 0; i < 8; i++) {
				gwfft_weights3 (gwdata->dd_data, i, weights, NULL, inverse_weights);
				weights++;
				inverse_weights++;
			}
		}

/* For the negacyclic pass 1 FFT wrapper, we combine the premultiplier sine with the column weight to save a few clocks. */
/* In the negacyclic pass 1 unFFT wrapper, we must multiply the precomputed premultiplier sine * weight by 1/weight^2 to get */
/* the premultiplier sine * inverse column weight. */
/* Compute the roots-of-minus-one premultipliers.  The root-of-minus-one premultiplier is for 2N, and a root-of-minus-one-of-2N is */
/* the same as a root unity for 4N.  We output the cosine/sine value and the sine value premultiplied by the column weight. */

		else {
			double	*inverse_weights, colweights[128];

			/* Generate the 8 inverse column weights (squared) */
			inverse_weights = table;
			table = inverse_weights + 8;
			for (i = 0; i < 8; i++) *inverse_weights++ = gwfft_weight_inverse_squared (gwdata->dd_data, i);
			/* Generate the 8 column weights in a scratch area */
			gwfft_colweights (gwdata->dd_data, &colweights, 8);
			/* Generate the complex premultipliers times the column weights */
			N = gwdata->FFTLEN / 2;			/* Number of complex values */
			upper_avx512_word = 8;
			for (j = 0; j < gwdata->FFTLEN / 2; j += 8 * upper_avx512_word) {
				gwsincos1plus01234567by8_colweighted (j, 1, N * 4, &colweights, table);
				gwsincos1plus01234567by8_colweighted (j + upper_avx512_word, 1, N * 4, &colweights, table + 1);
				gwsincos1plus01234567by8_colweighted (j + 2 * upper_avx512_word, 1, N * 4, &colweights, table + 2);
				gwsincos1plus01234567by8_colweighted (j + 3 * upper_avx512_word, 1, N * 4, &colweights, table + 3);
				gwsincos1plus01234567by8_colweighted (j + 4 * upper_avx512_word, 1, N * 4, &colweights, table + 4);
				gwsincos1plus01234567by8_colweighted (j + 5 * upper_avx512_word, 1, N * 4, &colweights, table + 5);
				gwsincos1plus01234567by8_colweighted (j + 6 * upper_avx512_word, 1, N * 4, &colweights, table + 6);
				gwsincos1plus01234567by8_colweighted (j + 7 * upper_avx512_word, 1, N * 4, &colweights, table + 7);
				table += 128;
			}
		}

/* Reserve space for the group multiplier fudge flags. There are FFTLEN fudge flags which takes FFTLEN / 8 bytes. */
/* One pass FFTs do not compress the fudge flags and one-eighth of the fudge flags are known to be zero and not output. */
/* For zero-padded FFTs, the fudge flags could be half the size, but we have not implemented that. */

		table = (double *) ((char *) table + gwdata->FFTLEN / 8 * 7 / 8);

/* Reserve space for the big/little flags. There are FFTLEN big/lit flags which takes FFTLEN / 8 bytes. */
/* The big/lit flags will be compressed at an 8:1 ratio, thus FFTLEN / 64 bytes are needed. */
/* Rational FFTs have no big/lit table, zero-padded FFTs have a half-size big/lit table. */

		gwdata->biglit_data_offset = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
		if (gwdata->RATIONAL_FFT);
		else if (gwdata->ZERO_PADDED_FFT) table = (double *) ((char *) table + gwdata->FFTLEN / 64 / 2);
		else table = (double *) ((char *) table + gwdata->FFTLEN / 64);

/* Round memory usage to a multiple of the cache line size */

		table = round_to_cache_line(table);
		gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);

/* Return address of the end of the table */

		return (table);
	}

/* Initialize some needed constants */

	pass1_size = gwdata->PASS1_SIZE;
	upper_avx512_word = gwdata->PASS2_SIZE;
	pass1_increment = gwdata->PASS2_SIZE * 8;

/* Determine number of delay groups.  In a standard radix-8 FFT, there is only one sin/cos */
/* group in the last pass 1 level.  We reduce our memory usage by using a fixed sin/cos */
/* table in the first FFT levels and having multiple groups of sin/cos data in the last pass 1 level. */
/* I call these groups of sin/cos data in the last pass 1 level "delay groups". */

	delay_count = zr4dwpn_delay_count (gwdata);

/* Loop through all the pass 1 groups in the same order the assembly code will process the groups. */

	for (group = 0; group < upper_avx512_word; group += gwdata->PASS1_CACHE_LINES) {

		pass1_size = gwdata->PASS1_SIZE;
		pass1_size /= (delay_count * 2);	/* Complex values we're generating sin/cos data for */
		N = gwdata->PASS2_SIZE;

/* Output the sin/cos/premultiplier values for the radix-8 block that does the last 3 levels in pass 1. */

		N = N * 8;

/* For the zr8_rsc_wpn_sgreg_eight_complex and zr8_rsc_wpn_sgreg_sixteen_reals building blocks output */
/* a separate table of column normalization values before the sin/cos data for the complex delay groups. */
/* The weights and inverse weights are output in separate tables so that we can group data in cache lines better. */

		{
			double *weights, *inverse_weights;
			weights = table;
			inverse_weights = weights + gwdata->PASS1_CACHE_LINES;
			table = inverse_weights + gwdata->PASS1_CACHE_LINES;
			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
				gwfft_weights3 (gwdata->dd_data, group + i, weights, NULL, inverse_weights);
				weights++;
				inverse_weights++;
			}
		}

/* Output the complex sin/cos values needed for a standard zr8sg_eight_complex_djbfft */
/* in the last pass 1 levels.  At runtime, we compute the actual sin/cos values from these. */

		for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 8) {
			// Asm code swizzles the input so that upper_avx512_word is 1
			temp = group + i;
			gwsincos1234by8_raw (temp, N, table);
			gwsincos1234by8_raw (temp + 1, N, table+1);
			gwsincos1234by8_raw (temp + 2, N, table+2);
			gwsincos1234by8_raw (temp + 3, N, table+3);
			gwsincos1234by8_raw (temp + 4, N, table+4);
			gwsincos1234by8_raw (temp + 5, N, table+5);
			gwsincos1234by8_raw (temp + 6, N, table+6);
			gwsincos1234by8_raw (temp + 7, N, table+7);
			table += 64;
		}

/* For the zr8sg_sixteen_reals_fft8 building block, output the extra */
/* sin/cos values needed for the sixteen_reals. */

		if (!gwdata->NEGACYCLIC_FFT) {
			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 8) {
//bug - would gwsincos1357by8 work here?  If not, why not.
				// Asm code swizzles the input so that upper_avx512_word is 1
				temp = group + i;
				gwsincos159Dby8 (temp, N*2, table);
				gwsincos159Dby8 (temp + 1, N*2, table+1);
				gwsincos159Dby8 (temp + 2, N*2, table+2);
				gwsincos159Dby8 (temp + 3, N*2, table+3);
				gwsincos159Dby8 (temp + 4, N*2, table+4);
				gwsincos159Dby8 (temp + 5, N*2, table+5);
				gwsincos159Dby8 (temp + 6, N*2, table+6);
				gwsincos159Dby8 (temp + 7, N*2, table+7);
				table += 64;
			}
		}

/* Output the sin/cos values for the complex delay groups -- specifically the zr8sg_rsc_eight_complex_fft8 macro. */

		for (k = 0; k < delay_count; k++) {
			if (k == 0 && !gwdata->NEGACYCLIC_FFT) continue;
			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 8) {
				unsigned long bigN, ktemp, actemp, avx512_word;

/* Work on each AVX-512 word */

				for (avx512_word = 0; avx512_word < 8; avx512_word++) {
					unsigned long final_group = group + i + avx512_word;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply part of the negacyclic premultiplier here. */

					if (gwdata->NEGACYCLIC_FFT) {
						bigN = gwdata->FFTLEN * 2;
						actemp = final_group;
					} else {
						bigN = gwdata->FFTLEN;
						actemp = 0;
					}

/* Factor in the delayed part of the sin/cos multiplies from the first levels.  In the first levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries. */

					if (gwdata->NEGACYCLIC_FFT) {
						if (k <= delay_count/2) ktemp = k * final_group * 4;
						else ktemp = bigN - (delay_count - k) * final_group * 4;
					}
					else {
						ktemp = k * final_group;
					}

/* We now calculate the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

					gwsincos1by8_raw (actemp + ktemp, bigN, table + avx512_word);
				}
				table += 16;
			}
		}
		pass1_size /= 8;

/* For the zr12_twelve_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size == 12) {
			N = N * 12;

/* Output the sin/cos data for the complex sections, (the zr12_twelve_complex_djbfft building block). */

			for (j = 0; j < N / 12; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos123456by8 (temp, N, table);
					gwsincos123456by8 (temp + upper_avx512_word, N, table+1);
					gwsincos123456by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos123456by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos123456by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos123456by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos123456by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos123456by8 (temp + 7 * upper_avx512_word, N, table+7);
					table += 96;

/* The zr12_csc_twentyfour_real building blocks require extra sin/cos values.  The twentyfour_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos13579Bby8 (temp, N*2, table);
						gwsincos13579Bby8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos13579Bby8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos13579Bby8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos13579Bby8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos13579Bby8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos13579Bby8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos13579Bby8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 96;
					}
				}
			}
			pass1_size /= 12;
		}

/* For the zr10_ten_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size == 10) {
			N = N * 10;

/* Output the sin/cos data for the complex sections, (the zr10_ten_complex_djbfft building block). */

			for (j = 0; j < N / 10; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12345by8 (temp, N, table);
					gwsincos12345by8 (temp + upper_avx512_word, N, table+1);
					gwsincos12345by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos12345by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos12345by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos12345by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos12345by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos12345by8 (temp + 7 * upper_avx512_word, N, table+7);
					table += 80;

/* The zr10_csc_twenty_real building blocks require extra sin/cos values.  The twenty_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos13579by8 (temp, N*2, table);
						gwsincos13579by8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos13579by8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos13579by8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos13579by8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos13579by8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos13579by8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos13579by8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 80;
					}
				}
			}
			pass1_size /= 10;
		}

/* For the zr5_five_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 5 == 0) {
			N = N * 5;

/* Output the sin/cos data for the complex sections, (the zr5_five_complex_djbfft building block). */

			for (j = 0; j < N / 5; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by8 (temp, N, table);
					gwsincos12by8 (temp + upper_avx512_word, N, table+1);
					gwsincos12by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos12by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos12by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos12by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos12by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos12by8 (temp + 7 * upper_avx512_word, N, table+7);
					table += 32;

/* The zr5_csc_ten_real building blocks require extra sin/cos values.  The ten_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos13by8 (temp, N*2, table);
						gwsincos13by8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos13by8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos13by8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos13by8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos13by8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos13by8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos13by8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 32;
					}
				}
			}
			pass1_size /= 5;
		}

/* For the zr6_six_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 6 == 0) {
			N = N * 6;

/* Output the sin/cos data for the complex sections, (the zr6_six_complex_djbfft building block). */

			for (j = 0; j < N / 6; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos123by8 (temp, N, table);
					gwsincos123by8 (temp + upper_avx512_word, N, table+1);
					gwsincos123by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos123by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos123by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos123by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos123by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos123by8 (temp + 7 * upper_avx512_word, N, table+7);
					table += 48;

/* The zr6_csc_twelve_real building blocks require extra sin/cos values.  The twelve_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos135by8 (temp, N*2, table);
						gwsincos135by8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos135by8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos135by8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos135by8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos135by8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos135by8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos135by8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 48;
					}
				}
			}
			pass1_size /= 6;
		}

/* For the zr7_seven_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 7 == 0) {
			N = N * 7;

/* Output the sin/cos data for the complex sections, (the zr7_seven_complex_djbfft building block). */
/* Use the special7 version which multiplies sine values by .975 which saves 2 clocks in 14 reals building block. */

			for (j = 0; j < N / 7; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos123by8_special7 (temp, N, table);
					gwsincos123by8_special7 (temp + upper_avx512_word, N, table+1);
					gwsincos123by8_special7 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos123by8_special7 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos123by8_special7 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos123by8_special7 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos123by8_special7 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos123by8_special7 (temp + 7 * upper_avx512_word, N, table+7);
					table += 48;

/* The zr7_csc_fourteen_real building blocks require extra sin/cos values.  The fourteen_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos135by8_special7 (temp, N*2, table);
						gwsincos135by8_special7 (temp + upper_avx512_word, N*2, table+1);
						gwsincos135by8_special7 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos135by8_special7 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos135by8_special7 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos135by8_special7 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos135by8_special7 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos135by8_special7 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 48;
					}
				}
			}
			pass1_size /= 7;
		}

/* For the zr16_sixteen_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size == 16) {
			N = N * 16;

/* Output the sin/cos data for the complex sections, (the zr16_sixteen_complex_djbfft building block). */

			for (j = 0; j < N / 16; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12345678by8 (temp, N, table);
					gwsincos12345678by8 (temp + upper_avx512_word, N, table+1);
					gwsincos12345678by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos12345678by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos12345678by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos12345678by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos12345678by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos12345678by8 (temp + 7 * upper_avx512_word, N, table+7);
					table += 128;

/* The zr16_csc_thirtytwo_real building blocks require extra sin/cos values.  The thirtytwo_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos13579BDFby8 (temp, N*2, table);
						gwsincos13579BDFby8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos13579BDFby8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos13579BDFby8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos13579BDFby8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos13579BDFby8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos13579BDFby8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos13579BDFby8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 128;
					}
				}
			}
			pass1_size /= 16;
		}

/* For the zr8_eight_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 8 == 0) {
			N = N * 8;

/* Output the sin/cos data for the complex sections, (the zr8_eight_complex_djbfft building block). */

			for (j = 0; j < N / 8; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos1234by8 (temp, N, table);
					gwsincos1234by8 (temp + upper_avx512_word, N, table+1);
					gwsincos1234by8 (temp + 2 * upper_avx512_word, N, table+2);
					gwsincos1234by8 (temp + 3 * upper_avx512_word, N, table+3);
					gwsincos1234by8 (temp + 4 * upper_avx512_word, N, table+4);
					gwsincos1234by8 (temp + 5 * upper_avx512_word, N, table+5);
					gwsincos1234by8 (temp + 6 * upper_avx512_word, N, table+6);
					gwsincos1234by8 (temp + 7 * upper_avx512_word, N, table+7);
#ifdef TRY_SQRT2_TO_REDUCE_ROUNDOFF
{
	gwsincos1234by8_sqrthalf (temp, N, table);
	gwsincos1234by8_sqrthalf (temp + upper_avx512_word, N, table+1);
	gwsincos1234by8_sqrthalf (temp + 2 * upper_avx512_word, N, table+2);
	gwsincos1234by8_sqrthalf (temp + 3 * upper_avx512_word, N, table+3);
	gwsincos1234by8_sqrthalf (temp + 4 * upper_avx512_word, N, table+4);
	gwsincos1234by8_sqrthalf (temp + 5 * upper_avx512_word, N, table+5);
	gwsincos1234by8_sqrthalf (temp + 6 * upper_avx512_word, N, table+6);
	gwsincos1234by8_sqrthalf (temp + 7 * upper_avx512_word, N, table+7);
}
#endif
					table += 64;

/* The zr8_csc_sixteen_real building blocks require extra sin/cos values.  The sixteen_real doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos1357by8 (temp, N*2, table);
						gwsincos1357by8 (temp + upper_avx512_word, N*2, table+1);
						gwsincos1357by8 (temp + 2 * upper_avx512_word, N*2, table+2);
						gwsincos1357by8 (temp + 3 * upper_avx512_word, N*2, table+3);
						gwsincos1357by8 (temp + 4 * upper_avx512_word, N*2, table+4);
						gwsincos1357by8 (temp + 5 * upper_avx512_word, N*2, table+5);
						gwsincos1357by8 (temp + 6 * upper_avx512_word, N*2, table+6);
						gwsincos1357by8 (temp + 7 * upper_avx512_word, N*2, table+7);
						table += 64;
					}
				}
			}
			pass1_size /= 8;
		}

/* Reserve space for the group multiplier fudge flags. There are pass1_size fudge flags, which takes pass1_size / 8 bytes. */
/* The fudge flags will be compressed at an 8:1 ratio, thus pass1_size / 64 bytes are needed. */
/* For zero-padded FFTs, the fudge flags could be half the size, but we have not implemented that. */

		table = (double *) ((char *) table + gwdata->PASS1_SIZE * gwdata->PASS1_CACHE_LINES / 64);

/* Reserve space for the big/little flags. There are pass1_size big/lit flags, which takes pass1_size / 8 bytes. */
/* The big/lit flags will be compressed at an 8:1 ratio, thus pass1_size / 64 bytes are needed. */
/* Rational FFTs have no big/lit table, zero-padded FFTs have a half-size big/lit table. */

		if (group == 0) gwdata->biglit_data_offset = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
		if (gwdata->RATIONAL_FFT);
		else if (gwdata->ZERO_PADDED_FFT) table = (double *) ((char *) table + gwdata->PASS1_SIZE * gwdata->PASS1_CACHE_LINES / 64 / 2);
		else table = (double *) ((char *) table + gwdata->PASS1_SIZE * gwdata->PASS1_CACHE_LINES / 64);

/* Round pass 1 group's memory usage to a multiple of the cache line size */
/* Calculate the size of each pass 1 group's sin/cos/premult data for pass1_get_next_block */

		table = round_to_cache_line(table);
		if (group == 0) gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the fixed sin/cos table used in pass 1 of the radix-8 delayed DJB FFT. */

double *zr4dwpn_build_fixed_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long upper_avx512_word, pass1_increment, pass1_size, i, j, N, delay_count;

/* Initialize some needed constants */

	upper_avx512_word = gwdata->PASS2_SIZE;
	pass1_increment = 8 * gwdata->PASS2_SIZE;
	pass1_size = gwdata->PASS1_SIZE;
	delay_count = zr4dwpn_delay_count (gwdata);

/* Real FFTs output one shared set of sin/cos values for the first 16-reals or 24-reals FFT */

	if (! gwdata->NEGACYCLIC_FFT) {
		N = gwdata->FFTLEN;
		if (delay_count == 8) {
			for (j = 0; j < N / 16; j += pass1_increment) {
				for (i = 1; i <= 7; i++) {	/* Create 7 twiddle factors */
					gwsincos1by8 (i * j, N, table);
					gwsincos1by8 (i * (j + upper_avx512_word), N, table+1);
					gwsincos1by8 (i * (j + 2 * upper_avx512_word), N, table+2);
					gwsincos1by8 (i * (j + 3 * upper_avx512_word), N, table+3);
					gwsincos1by8 (i * (j + 4 * upper_avx512_word), N, table+4);
					gwsincos1by8 (i * (j + 5 * upper_avx512_word), N, table+5);
					gwsincos1by8 (i * (j + 6 * upper_avx512_word), N, table+6);
					gwsincos1by8 (i * (j + 7 * upper_avx512_word), N, table+7);
					table += 16;
				}
			}
			N = N / 16;
		}
		if (delay_count == 12) {
			for (j = 0; j < N / 24; j += pass1_increment) {
				for (i = 1; i <= 11; i++) {	/* Create 11 twiddle factors */
					gwsincos1by8 (i * j, N, table);
					gwsincos1by8 (i * (j + upper_avx512_word), N, table+1);
					gwsincos1by8 (i * (j + 2 * upper_avx512_word), N, table+2);
					gwsincos1by8 (i * (j + 3 * upper_avx512_word), N, table+3);
					gwsincos1by8 (i * (j + 4 * upper_avx512_word), N, table+4);
					gwsincos1by8 (i * (j + 5 * upper_avx512_word), N, table+5);
					gwsincos1by8 (i * (j + 6 * upper_avx512_word), N, table+6);
					gwsincos1by8 (i * (j + 7 * upper_avx512_word), N, table+7);
					table += 16;
				}
			}
			N = N / 24;
		}
	}

/* For negacyclic FFTs, build the fixed roots-of-minus-one table and the DJB FFT sin/cos table. */
/* Output these values in the same order they will be used in the first levels of pass 1. */

	else {
		N = gwdata->FFTLEN / 2;

// BUG - delay count of 5 is not working yet
		if (delay_count == 5) {
			for (j = 0; j < N / 5; j += pass1_increment) {
				/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one */
				/* premultiplier is for 2N, and a root-of-minus-one-of-2N is the same as */
				/* a root unity for 4N. */
				gwsincos1plus01234by8 (j, N / 5, N * 4, table);
				gwsincos1plus01234by8 (j + upper_avx512_word, N / 5, N * 4, table + 1);
				gwsincos1plus01234by8 (j + 2 * upper_avx512_word, N / 5, N * 4, table + 2);
				gwsincos1plus01234by8 (j + 3 * upper_avx512_word, N / 5, N * 4, table + 3);
				gwsincos1plus01234by8 (j + 4 * upper_avx512_word, N / 5, N * 4, table + 4);
				gwsincos1plus01234by8 (j + 5 * upper_avx512_word, N / 5, N * 4, table + 5);
				gwsincos1plus01234by8 (j + 6 * upper_avx512_word, N / 5, N * 4, table + 6);
				gwsincos1plus01234by8 (j + 7 * upper_avx512_word, N / 5, N * 4, table + 7);
				table += 80;
				/* Output the fixed sin/cos DJB FFT entry */
				gwsincos12by8 (j, N, table);
				gwsincos12by8 (j + upper_avx512_word, N, table+1);
				gwsincos12by8 (j + 2 * upper_avx512_word, N, table+2);
				gwsincos12by8 (j + 3 * upper_avx512_word, N, table+3);
				gwsincos12by8 (j + 4 * upper_avx512_word, N, table+4);
				gwsincos12by8 (j + 5 * upper_avx512_word, N, table+5);
				gwsincos12by8 (j + 6 * upper_avx512_word, N, table+6);
				gwsincos12by8 (j + 7 * upper_avx512_word, N, table+7);
				table += 32;
			}
			N = N / 5;
		}

		if (delay_count == 8) {
			for (j = 0; j < N / 8; j += pass1_increment) {
				/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one premultiplier is */
				/* for 2N, and a root-of-minus-one-of-2N is the same as a root unity for 4N.  NOTE: We only */
				/* output the cos/sin values, the sine value will be applied to the group multipliers later on. */
				gwcos1plus01234567by8 (j, N / 8, N * 4, table);
				gwcos1plus01234567by8 (j + upper_avx512_word, N / 8, N * 4, table + 1);
				gwcos1plus01234567by8 (j + 2 * upper_avx512_word, N / 8, N * 4, table + 2);
				gwcos1plus01234567by8 (j + 3 * upper_avx512_word, N / 8, N * 4, table + 3);
				gwcos1plus01234567by8 (j + 4 * upper_avx512_word, N / 8, N * 4, table + 4);
				gwcos1plus01234567by8 (j + 5 * upper_avx512_word, N / 8, N * 4, table + 5);
				gwcos1plus01234567by8 (j + 6 * upper_avx512_word, N / 8, N * 4, table + 6);
				gwcos1plus01234567by8 (j + 7 * upper_avx512_word, N / 8, N * 4, table + 7);
				table += 64;
				/* Output the fixed sin/cos DJB FFT entry */
				gwsincos1234by8 (j, N, table);
				gwsincos1234by8 (j + upper_avx512_word, N, table+1);
				gwsincos1234by8 (j + 2 * upper_avx512_word, N, table+2);
				gwsincos1234by8 (j + 3 * upper_avx512_word, N, table+3);
				gwsincos1234by8 (j + 4 * upper_avx512_word, N, table+4);
				gwsincos1234by8 (j + 5 * upper_avx512_word, N, table+5);
				gwsincos1234by8 (j + 6 * upper_avx512_word, N, table+6);
				gwsincos1234by8 (j + 7 * upper_avx512_word, N, table+7);
#ifdef TRY_SQRT2_TO_REDUCE_ROUNDOFF
{
	gwsincos1234by8_sqrthalf (j, N, table);
	gwsincos1234by8_sqrthalf (j + upper_avx512_word, N, table+1);
	gwsincos1234by8_sqrthalf (j + 2 * upper_avx512_word, N, table+2);
	gwsincos1234by8_sqrthalf (j + 3 * upper_avx512_word, N, table+3);
	gwsincos1234by8_sqrthalf (j + 4 * upper_avx512_word, N, table+4);
	gwsincos1234by8_sqrthalf (j + 5 * upper_avx512_word, N, table+5);
	gwsincos1234by8_sqrthalf (j + 6 * upper_avx512_word, N, table+6);
	gwsincos1234by8_sqrthalf (j + 7 * upper_avx512_word, N, table+7);
}
#endif
				table += 64;
			}
			N = N / 8;
		}

		if (delay_count == 12) {
			for (j = 0; j < N / 12; j += pass1_increment) {
				/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one premultiplier is */
				/* for 2N, and a root-of-minus-one-of-2N is the same as a root unity for 4N.  NOTE: We only */
				/* output the cos/sin values, the sine value will be applied to the group multipliers later on. */
				gwcos1plus0123456789ABby8 (j, N / 12, N * 4, table);
				gwcos1plus0123456789ABby8 (j + upper_avx512_word, N / 12, N * 4, table + 1);
				gwcos1plus0123456789ABby8 (j + 2 * upper_avx512_word, N / 12, N * 4, table + 2);
				gwcos1plus0123456789ABby8 (j + 3 * upper_avx512_word, N / 12, N * 4, table + 3);
				gwcos1plus0123456789ABby8 (j + 4 * upper_avx512_word, N / 12, N * 4, table + 4);
				gwcos1plus0123456789ABby8 (j + 5 * upper_avx512_word, N / 12, N * 4, table + 5);
				gwcos1plus0123456789ABby8 (j + 6 * upper_avx512_word, N / 12, N * 4, table + 6);
				gwcos1plus0123456789ABby8 (j + 7 * upper_avx512_word, N / 12, N * 4, table + 7);
				table += 96;
				/* Output the fixed sin/cos DJB FFT entry */
				gwsincos123456by8 (j, N, table);
				gwsincos123456by8 (j + upper_avx512_word, N, table+1);
				gwsincos123456by8 (j + 2 * upper_avx512_word, N, table+2);
				gwsincos123456by8 (j + 3 * upper_avx512_word, N, table+3);
				gwsincos123456by8 (j + 4 * upper_avx512_word, N, table+4);
				gwsincos123456by8 (j + 5 * upper_avx512_word, N, table+5);
				gwsincos123456by8 (j + 6 * upper_avx512_word, N, table+6);
				gwsincos123456by8 (j + 7 * upper_avx512_word, N, table+7);
				table += 96;
			}
			N = N / 12;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* Build the sin/cos table used in complex pass 2 blocks in a traditional radix-8 FFT. */

double *zr4_build_pass2_complex_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, N;

	N = gwdata->PASS2_SIZE;

/* If the pass 2 size is divisible by 5, then the initial levels do */
/* radix-5 steps which requires two sin/cos values.  The first levels */
/* in pass 2 have an upper_avx512_word of one. */

	if (gwdata->PASS2_SIZE != 640 && gwdata->PASS2_SIZE != 4480 && gwdata->PASS2_SIZE != 6400 &&
	    gwdata->PASS2_SIZE != 7680 && gwdata->PASS2_SIZE != 10240) {
		while (N % 5 == 0) {
			for (i = 0; i < N / 5; i += 8) {
				gwsincos12by8 (i, N, table);
				gwsincos12by8 (i+1, N, table+1);
				gwsincos12by8 (i+2, N, table+2);
				gwsincos12by8 (i+3, N, table+3);
				gwsincos12by8 (i+4, N, table+4);
				gwsincos12by8 (i+5, N, table+5);
				gwsincos12by8 (i+6, N, table+6);
				gwsincos12by8 (i+7, N, table+7);
				table += 32;
			}
			N = N / 5;
			if (N == 10*64) break;  // For 5 * 10 * 64 pass 2 size
		}
	}

/* If the pass 2 size is divisible by 7, then the initial levels do radix-7 steps which requires three sin/cos values. */
/* The first levels in pass 2 have an upper_avx512_word of one.  Use the special7 version which multiplies sine values */
/* by .975 to save 2 clocks in 14 reals building block. */

	while (N % 7 == 0) {
		for (i = 0; i < N / 7; i += 8) {
			gwsincos123by8_special7 (i, N, table);
			gwsincos123by8_special7 (i+1, N, table+1);
			gwsincos123by8_special7 (i+2, N, table+2);
			gwsincos123by8_special7 (i+3, N, table+3);
			gwsincos123by8_special7 (i+4, N, table+4);
			gwsincos123by8_special7 (i+5, N, table+5);
			gwsincos123by8_special7 (i+6, N, table+6);
			gwsincos123by8_special7 (i+7, N, table+7);
			table += 48;
		}
		N = N / 7;
	}

/* If the pass 2 size is divisible by 10, then the initial levels do */
/* radix-10 steps which requires 5 sin/cos values.  The first levels */
/* in pass 2 have an upper_avx512_word of one. */

	while (N % 10 == 0) {
		for (i = 0; i < N / 10; i += 8) {
			gwsincos12345by8 (i, N, table);
			gwsincos12345by8 (i+1, N, table+1);
			gwsincos12345by8 (i+2, N, table+2);
			gwsincos12345by8 (i+3, N, table+3);
			gwsincos12345by8 (i+4, N, table+4);
			gwsincos12345by8 (i+5, N, table+5);
			gwsincos12345by8 (i+6, N, table+6);
			gwsincos12345by8 (i+7, N, table+7);
			table += 80;
		}
		N = N / 10;
	}

/* If the pass 2 size is divisible by 12, then the initial levels do */
/* radix-12 steps which requires six sin/cos values.  The first levels */
/* in pass 2 have an upper_avx512_word of one. */

	while (N == 12*64 || N == 12*6*64 || N == 12*8*64 || N == 12*12*64 || N == 12*16*64) {
		for (i = 0; i < N / 12; i += 8) {
			gwsincos123456by8 (i, N, table);
			gwsincos123456by8 (i+1, N, table+1);
			gwsincos123456by8 (i+2, N, table+2);
			gwsincos123456by8 (i+3, N, table+3);
			gwsincos123456by8 (i+4, N, table+4);
			gwsincos123456by8 (i+5, N, table+5);
			gwsincos123456by8 (i+6, N, table+6);
			gwsincos123456by8 (i+7, N, table+7);
			table += 96;
		}
		N = N / 12;
	}

/* If the pass 2 size is divisible by 6, then the initial levels do */
/* radix-6 steps which requires three sin/cos values.  The first levels */
/* in pass 2 have an upper_avx512_word of one. */

	while (N % 6 == 0) {
		for (i = 0; i < N / 6; i += 8) {
			gwsincos123by8 (i, N, table);
			gwsincos123by8 (i+1, N, table+1);
			gwsincos123by8 (i+2, N, table+2);
			gwsincos123by8 (i+3, N, table+3);
			gwsincos123by8 (i+4, N, table+4);
			gwsincos123by8 (i+5, N, table+5);
			gwsincos123by8 (i+6, N, table+6);
			gwsincos123by8 (i+7, N, table+7);
			table += 48;
		}
		N = N / 6;
	}

/* For any radix-16 blocks, output eight sin/cos values */

	while (N == 16*64 || N == 16*8*64 || N == 16*16*64) {
		for (i = 0; i < N / 16; i += 8) {
			gwsincos12345678by8 (i, N, table);
			gwsincos12345678by8 (i+1, N, table+1);
			gwsincos12345678by8 (i+2, N, table+2);
			gwsincos12345678by8 (i+3, N, table+3);
			gwsincos12345678by8 (i+4, N, table+4);
			gwsincos12345678by8 (i+5, N, table+5);
			gwsincos12345678by8 (i+6, N, table+6);
			gwsincos12345678by8 (i+7, N, table+7);
			table += 128;
		}
		N = N / 16;
	}

/* For the remaining radix-8 blocks, output four sin/cos values */

	while (N > 8) {
		for (i = 0; i < N / 8; i += 8) {
			gwsincos1234by8 (i, N, table);
			gwsincos1234by8 (i+1, N, table+1);
			gwsincos1234by8 (i+2, N, table+2);
			gwsincos1234by8 (i+3, N, table+3);
			gwsincos1234by8 (i+4, N, table+4);
			gwsincos1234by8 (i+5, N, table+5);
			gwsincos1234by8 (i+6, N, table+6);
			gwsincos1234by8 (i+7, N, table+7);
#ifdef TRY_SQRT2_TO_REDUCE_ROUNDOFF
if (N > 64) {
	gwsincos1234by8_sqrthalf (i, N, table);
	gwsincos1234by8_sqrthalf (i+1, N, table+1);
	gwsincos1234by8_sqrthalf (i+2, N, table+2);
	gwsincos1234by8_sqrthalf (i+3, N, table+3);
	gwsincos1234by8_sqrthalf (i+4, N, table+4);
	gwsincos1234by8_sqrthalf (i+5, N, table+5);
	gwsincos1234by8_sqrthalf (i+6, N, table+6);
	gwsincos1234by8_sqrthalf (i+7, N, table+7);
}
#endif
			table += 64;
		}
		N = N / 8;
	}

/* Return address of the end of the table */

	return (table);
}

/* Build the sin/cos table used in pass 2 of real FFTs */

double *zr4_build_pass2_real_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long avx512_increment, j, N;

/* Negacyclic FFTs, don't need these tables */

	if (gwdata->NEGACYCLIC_FFT) return (table);

/* Init */

	avx512_increment = 1;
	N = gwdata->PASS2_SIZE;

/* Output sin/cos values for 10-real macros */

	if (gwdata->PASS2_SIZE != 640 && gwdata->PASS2_SIZE != 4480 && gwdata->PASS2_SIZE != 6400 &&
	    gwdata->PASS2_SIZE != 7680 && gwdata->PASS2_SIZE != 10240) {
		while (N % 5 == 0) {
			for (j = 0; j < N / 5; j += 8) {
				gwsincos13by8 (j, N*2, table);
				gwsincos13by8 (j + avx512_increment, N*2, table+1);
				gwsincos13by8 (j + 2*avx512_increment, N*2, table+2);
				gwsincos13by8 (j + 3*avx512_increment, N*2, table+3);
				gwsincos13by8 (j + 4*avx512_increment, N*2, table+4);
				gwsincos13by8 (j + 5*avx512_increment, N*2, table+5);
				gwsincos13by8 (j + 6*avx512_increment, N*2, table+6);
				gwsincos13by8 (j + 7*avx512_increment, N*2, table+7);
				table += 32;
			}
			N = N / 5;
			if (N == 10*64) break;
		}
	}

/* Output sin/cos values for 14-real macros.  Use the special7 version which multiplies sine values by .975 to save 2 clocks in 14 reals building block. */

	while (N % 7 == 0) {
		for (j = 0; j < N / 7; j += 8) {
			gwsincos135by8_special7 (j, N*2, table);
			gwsincos135by8_special7 (j + avx512_increment, N*2, table+1);
			gwsincos135by8_special7 (j + 2*avx512_increment, N*2, table+2);
			gwsincos135by8_special7 (j + 3*avx512_increment, N*2, table+3);
			gwsincos135by8_special7 (j + 4*avx512_increment, N*2, table+4);
			gwsincos135by8_special7 (j + 5*avx512_increment, N*2, table+5);
			gwsincos135by8_special7 (j + 6*avx512_increment, N*2, table+6);
			gwsincos135by8_special7 (j + 7*avx512_increment, N*2, table+7);
			table += 48;
		}
		N = N / 7;
	}

/* Output sin/cos values for 20-real macros */

	while (N % 10 == 0) {
		for (j = 0; j < N / 10; j += 8) {
			gwsincos13579by8 (j, N*2, table);
			gwsincos13579by8 (j + avx512_increment, N*2, table+1);
			gwsincos13579by8 (j + 2*avx512_increment, N*2, table+2);
			gwsincos13579by8 (j + 3*avx512_increment, N*2, table+3);
			gwsincos13579by8 (j + 4*avx512_increment, N*2, table+4);
			gwsincos13579by8 (j + 5*avx512_increment, N*2, table+5);
			gwsincos13579by8 (j + 6*avx512_increment, N*2, table+6);
			gwsincos13579by8 (j + 7*avx512_increment, N*2, table+7);
			table += 80;
		}
		N = N / 10;
	}

/* Output sin/cos values for 24-real macros */

	while (N == 12*64 || N == 12*6*64 || N == 12*8*64 || N == 12*12*64 || N == 12*16*64) {
		for (j = 0; j < N / 12; j += 8) {
			gwsincos13579Bby8 (j, N*2, table);
			gwsincos13579Bby8 (j + avx512_increment, N*2, table+1);
			gwsincos13579Bby8 (j + 2*avx512_increment, N*2, table+2);
			gwsincos13579Bby8 (j + 3*avx512_increment, N*2, table+3);
			gwsincos13579Bby8 (j + 4*avx512_increment, N*2, table+4);
			gwsincos13579Bby8 (j + 5*avx512_increment, N*2, table+5);
			gwsincos13579Bby8 (j + 6*avx512_increment, N*2, table+6);
			gwsincos13579Bby8 (j + 7*avx512_increment, N*2, table+7);
			table += 96;
		}
		N = N / 12;
	}

/* Output sin/cos values for 12-real macros */

	while (N % 6 == 0) {
		for (j = 0; j < N / 6; j += 8) {
			gwsincos135by8 (j, N*2, table);
			gwsincos135by8 (j + avx512_increment, N*2, table+1);
			gwsincos135by8 (j + 2*avx512_increment, N*2, table+2);
			gwsincos135by8 (j + 3*avx512_increment, N*2, table+3);
			gwsincos135by8 (j + 4*avx512_increment, N*2, table+4);
			gwsincos135by8 (j + 5*avx512_increment, N*2, table+5);
			gwsincos135by8 (j + 6*avx512_increment, N*2, table+6);
			gwsincos135by8 (j + 7*avx512_increment, N*2, table+7);
			table += 48;
		}
		N = N / 6;
	}

/* Output sin/cos values for remaining 32-real macros */

	while (N == 16*64 || N == 16*8*64 || N == 16*16*64) {
		for (j = 0; j < N / 16; j += 8) {
			gwsincos13579BDFby8 (j, N*2, table);
			gwsincos13579BDFby8 (j + avx512_increment, N*2, table+1);
			gwsincos13579BDFby8 (j + 2*avx512_increment, N*2, table+2);
			gwsincos13579BDFby8 (j + 3*avx512_increment, N*2, table+3);
			gwsincos13579BDFby8 (j + 4*avx512_increment, N*2, table+4);
			gwsincos13579BDFby8 (j + 5*avx512_increment, N*2, table+5);
			gwsincos13579BDFby8 (j + 6*avx512_increment, N*2, table+6);
			gwsincos13579BDFby8 (j + 7*avx512_increment, N*2, table+7);
			table += 128;
		}
		N = N / 16;
	}

/* Output sin/cos values for remaining 16-real macros */

	while (N > 8) {
		for (j = 0; j < N / 8; j += 8) {
			gwsincos1357by8 (j, N*2, table);
			gwsincos1357by8 (j + avx512_increment, N*2, table+1);
			gwsincos1357by8 (j + 2*avx512_increment, N*2, table+2);
			gwsincos1357by8 (j + 3*avx512_increment, N*2, table+3);
			gwsincos1357by8 (j + 4*avx512_increment, N*2, table+4);
			gwsincos1357by8 (j + 5*avx512_increment, N*2, table+5);
			gwsincos1357by8 (j + 6*avx512_increment, N*2, table+6);
			gwsincos1357by8 (j + 7*avx512_increment, N*2, table+7);
			table += 64;
		}
		N = N / 8;
	}

/* Return address of the end of the table */

	return (table);
}

/* Build the big/little flags table for an AVX-512 r4dwpn (radix-8 with partial normalization) FFT. */

double *zr4dwpn_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned char *p, *temp_biglit_table;
	unsigned long upper_avx512_word, group, h, i, j, k, num_combos, temp_biglit_table_size, varblock_biglit_data_size;
	uint64_t combos[129];		// There are 129 possible 8-byte combinations
	int	next_combo[129];
	uint64_t *output_ptr;		// Ptr for outputting the compressed biglit table
const	int	START_OF_CHAIN = 0x8000;
const	int	END_OF_CHAIN = 0x4000;

/* Init table of first 8 big/lit values */

	memset (asm_data->u.zmm.ZMM_FIRST_BIGLIT_VALUES, 0, sizeof (asm_data->u.zmm.ZMM_FIRST_BIGLIT_VALUES));

/* Rational FFTs don't have any big/lit flags */

	if (gwdata->RATIONAL_FFT) return (table);

/* Initialize.  There are remarkable similarities between the one-pass and two-pass table builds. */

	if (gwdata->PASS1_SIZE == 0) {
		upper_avx512_word = 8;
		// Calculate number of big/lit bytes written to each block of variable data
		varblock_biglit_data_size = gwdata->FFTLEN / 64;
	} else {
		upper_avx512_word = gwdata->PASS2_SIZE;
		// Calculate number of big/lit bytes written to each block of variable data
		varblock_biglit_data_size = gwdata->PASS1_SIZE * gwdata->PASS1_CACHE_LINES / 64;
	}
	if (gwdata->ZERO_PADDED_FFT) varblock_biglit_data_size = varblock_biglit_data_size / 2;

/* Loop to build table in exactly the same order that it will be used by the assembly code. */

	temp_biglit_table_size = gwdata->FFTLEN / 8;
	if (gwdata->ZERO_PADDED_FFT) temp_biglit_table_size = temp_biglit_table_size / 2;
	p = temp_biglit_table = (unsigned char *) malloc (temp_biglit_table_size);
	for (group = 0; group < upper_avx512_word; group += gwdata->PASS1_CACHE_LINES) {
	    for (h = 0; h < gwdata->FFTLEN / 2; h += 4 * 8 * upper_avx512_word) {
		for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
		    for (j = 0; j < 4 * 8 * upper_avx512_word; j += 8 * upper_avx512_word) {
			for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
				unsigned long word;

/* Zero-padded FFTs only need a half-sized big/lit table */

				if (k && gwdata->ZERO_PADDED_FFT) continue;

/* Now set the big/little flag corresponding to words in an AVX-512 cacheline */

				word = group + h + i + j + k;
				*p = is_big_word (gwdata, word);
				if (is_big_word (gwdata, word + upper_avx512_word)) *p += 2;
				if (is_big_word (gwdata, word + 2 * upper_avx512_word)) *p += 4;
				if (is_big_word (gwdata, word + 3 * upper_avx512_word)) *p += 8;
				if (is_big_word (gwdata, word + 4 * upper_avx512_word)) *p += 16;
				if (is_big_word (gwdata, word + 5 * upper_avx512_word)) *p += 32;
				if (is_big_word (gwdata, word + 6 * upper_avx512_word)) *p += 64;
				if (is_big_word (gwdata, word + 7 * upper_avx512_word)) *p += 128;

/* Create separate table for first 8 biglit values for carry propagation code */

				if (word < sizeof (asm_data->u.zmm.ZMM_FIRST_BIGLIT_VALUES) * 8 && (*p & 1))
					bitset (asm_data->u.zmm.ZMM_FIRST_BIGLIT_VALUES, word);

/* Move to next big/lit byte */

				p++;
			}
		    }
		}
	    }
	}

/* Now compress the table.  Big/lit flags form a very regular pattern.  For example, if there are 18.3 b's per */
/* FFT word then you get either a big word followed by two little words or a big word followed by three little words. */
/* Here we determine which patterns of big/lit are possible in a znorm macro which processes 4 AVX-512 words. */

/* Generate all possible valid combinations of big/lit flags */

	p = temp_biglit_table;
	num_combos = 0;
	for (i = 0; i < temp_biglit_table_size; i += 8) {
		uint64_t combo = * (uint64_t *) (p+i);
		/* Ignore this combo if it is a duplicate.  Otherwise, add it to our combos collection. */
		for (j = 0; ; j++) {
			if (j == num_combos) {
				combos[num_combos++] = combo;
				break;
			}
			if (combo == combos[j]) break;
		}
		/* Remember combo number for building biglit table later */
		p[i] = (unsigned char) j;
	}

/* Concatentate combos to save space. Look for 2 chains where the end of one chain has 4 bytes in common */
/* with the start of the other chain. */

	/* Init the next-in-chain array */
	for (i = 0; i < num_combos; i++) next_combo[i] = START_OF_CHAIN + END_OF_CHAIN;
	/* Examine all chain starts */
	for (i = 0; i < num_combos; i++) {
		int	chain_end;
		/* Skip if not the start of a chain */
		if (! (next_combo[i] & START_OF_CHAIN)) continue;
		/* Find end of chain */
		for (chain_end = i; ! (next_combo[chain_end] & END_OF_CHAIN); chain_end = next_combo[chain_end] & 0xFF);
		/* Now look at all chain ends */
		for (j = 0; j < num_combos; j++) {
			/* Skip if not a chain end */
			if (! (next_combo[j] & END_OF_CHAIN)) continue;
			/* Can't chain to ourselves! */
			if (j == chain_end) continue;
			/* See if chain end has common elements with the chain start */
			/* Due to little-endianness we want the MSW of the chain end to match the LSW of start */
			/* j(end)	LSW, MSW */
			/* i(strt)	     LSW  MSW */
			if ((combos[j] >> 32) == (combos[i] & 0xFFFFFFFF)) {
				next_combo[j] = (next_combo[j] & START_OF_CHAIN) + i;
				next_combo[i] &= ~START_OF_CHAIN;
				break;
			}
		}
	}

/* Output the compressed biglit table.  We believe this will always fit in 768 bytes. */

	asm_data->compressed_biglits = table;
	table = (double *) (((char *) table) + 768);
	output_ptr = (uint64_t *) asm_data->compressed_biglits;
	for (i = 0; i < num_combos; i++) {
		/* Skip if not the start of a chain */
		if (! (next_combo[i] & START_OF_CHAIN)) continue;
		/* Follow the chain */
		for (j = i; ; j = next_combo[j] & 0xFFF) {
			/* Output the combo */
			*output_ptr = combos[j];
			/* Remember this combo's "offset" into the compressed biglit table */
			/* We'll use this to build the biglit table later */
			combos[j] = ((char *) output_ptr - (char *) asm_data->compressed_biglits) / sizeof (uint32_t);
			/* Advance the output ptr */
			output_ptr++;
			/* Break when chain ends */
			if (next_combo[j] & END_OF_CHAIN) break;
			/* Back up the output ptr by half the distance */
			output_ptr = (uint64_t *) (((char *) output_ptr) - sizeof (uint32_t));
		}
	}
	ASSERTG ((char *) output_ptr <= (char *) table);

/* Output the indexes into the compressed biglit table */

	// Calculate address of biglit data in the first variable block's data.
	p = (unsigned char *) gwdata->pass1_var_data + gwdata->biglit_data_offset;
	// Init index into temp_biglit_table
	i = 0;
	// For each pass 1 block, output the proper number of bytes to the variable sin/cos data.
	for (group = 0; group < upper_avx512_word; group += gwdata->PASS1_CACHE_LINES) {
		for (k = 0; k < varblock_biglit_data_size; k++) {
			/* Get the combo number we saved earlier */
			j = temp_biglit_table[i];  i += 8;
			/* Convert the combo number into an index into the compressed biglit table.  Output it. */
			*p++ = (unsigned char) combos[j];
		}
		p += gwdata->pass1_var_data_size - varblock_biglit_data_size;
	}

/* Free the temporary table */

	free (temp_biglit_table);

/* Return pointer to end of our table */

	return (table);
}


/* Build the normalization table (FFT weights) used by radix-8 with partial normalization FFTs. */
/* We actually build the table twice.  The first time we allocate space and compute the group */
/* multipliers in the same order that the zr8/12_first_fft macros need them.  The second time */
/* we build the table in the same order that the normalize code needs the inverse group multipliers. */
/* The inverse group multipliers are not applied by the zr8/12_last_unfft macros because the normalize */
/* macros can apply the multipliers for free using FMA instructions. */

double *zr4dwpn_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the area to allocate from and fill in.  This pointer is NULL for second invocation. */
{
	unsigned long delay_count, i, j, k, avx512_word, upper_avx512_word, grp;
	double	*weights, *inverse_weights;

/* Initialize.  There are remarkable similarities between the one-pass and two-pass table builds. */

	if (gwdata->PASS1_SIZE == 0) {
		delay_count = 1;
		upper_avx512_word = 8;
	} else {
		delay_count = zr4dwpn_delay_count (gwdata);
		upper_avx512_word = gwdata->PASS2_SIZE;
	}

/* Allocate space for or reestablish pointers to previously allocated group multipliers */

	if (table != NULL) {	/* First build of the group multipliers, allocate space */
		weights = table;
		table = table + 2 * gwdata->FFTLEN / upper_avx512_word;
	} else {		/* Rebuilding the negacyclic group multipliers to apply roots-of-minus-one sine */
		struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
		weights = (double *) asm_data->norm_grp_mults;
	}
	inverse_weights = weights + gwdata->FFTLEN / upper_avx512_word;

/* First build of the tables.  Build the group multipliers in the same order as used by the first and last FFT levels */
/* where the group multipliers are applied.  NOTE:  The inverse group multipliers are no longer applied by the last FFT levels, */
/* they can be applied for free via FMA instructions during normalization.  NOTE2:  In a negacyclic FFT, we save a multiply by */
/* precomputing the group multiplier times the sine value of the roots-of-minus-one premultiplier.  We do not apply the sine value */
/* the first time the table is built so that we can properly build the XOR masks used in compressing the fudge factor flags. */
/* NOTE3: The weight associated with abs(c) != 1 is also not applied so that we can properly build the XOR masks. */
/* NOTE4: The sixteen real macros need some group multipliers divided by another group multiplier to save a multiply instruction (also delayed for XOR masks). */

	if (table != NULL) {
	    for (i = 0; i < gwdata->FFTLEN / 2 / delay_count; i += 8 * upper_avx512_word) {
		for (j = 0; j < gwdata->FFTLEN / 2; j += gwdata->FFTLEN / 2 / delay_count) {
		    for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
			for (avx512_word = 0; avx512_word < 8 * upper_avx512_word; avx512_word += upper_avx512_word) {

/* For zero-padded FFTs the upper half of the FFT has the exact same multipliers as the lower half. */
/* Thus we can cut the size of our group multiplier table in half. */

//BUG				if (gwdata->ZERO_PADDED_FFT && grp >= gwdata->FFTLEN / 2) continue;

/* Call quad-precision routine to compute group multipliers. */

				grp = i + j + k + avx512_word;
				*weights++ = gwfft_weight_no_c (gwdata->dd_data, grp);
			}
		    }
		}
	    }
	}

/* Second build of the tables.  The group multipliers must be rebuilt in the same order as used by the zr8/12_first_fft macros. */
/* This needs to be done for one-pass FFTs, two-pass negacyclic FFTs where we did not precompute the group multiplier times */
/* the sine value of the roots-of-minus-one premultiplier, and the case where abs(c) != 1. */

	else {
	    if (gwdata->PASS1_SIZE == 0 || (gwdata->PASS1_SIZE && gwdata->NEGACYCLIC_FFT) || labs (gwdata->c) != 1 || delay_count == 8 || delay_count == 12)
	    for (i = 0; i < gwdata->FFTLEN / 2 / delay_count; i += 8 * upper_avx512_word) {
		for (j = 0; j < gwdata->FFTLEN / 2; j += gwdata->FFTLEN / 2 / delay_count) {
		    for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
			for (avx512_word = 0; avx512_word < 8 * upper_avx512_word; avx512_word += upper_avx512_word) {
				double	ttp;
				unsigned long sine_word;

/* For zero-padded FFTs the upper half of the FFT has the exact same multipliers as the lower half. */
/* Thus we can cut the size of our group multiplier table in half. */

//BUG				if (gwdata->ZERO_PADDED_FFT && grp >= gwdata->FFTLEN / 2) continue;

/* Call quad-precision routine to compute group multipliers.  For one-pass FFTs, we used to save a multiply in the unfft wrapper */
/* by shifting the multiplication by 2/FFTLEN to the group multiplier but this corrupts FFT(1) which is bad for gwmulmuladd and polymult. */
/* For two-pass negacyclic FFTs, we save a multiply by precomputing the group multiplier times the sine value of the roots-of-minus-one premultiplier. */

				grp = i + j + k + avx512_word;
				if (gwdata->PASS1_SIZE && gwdata->NEGACYCLIC_FFT) {
					/* Handle case where we use FMA to save multiplies by pre-dividing group multipliers */
					if ((delay_count == 8 && k != 0 && j <= 3 * (gwdata->FFTLEN / 2 / delay_count)) ||
					    (delay_count == 12 && k != 0 && j <= 5 * (gwdata->FFTLEN / 2 / delay_count)))
						ttp = gwfft_weight_over_weight (gwdata->dd_data, grp, grp - k);
					/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one */
					/* premultiplier is for 2N, and a root-of-minus-one-of-2N is the same as */
					/* a root unity for 4N (where N is the number of complex values = FFTLEN/2). */
					else {
						sine_word = i + j + avx512_word;
						gwfft_weights_times_sine (gwdata->dd_data, grp, sine_word, gwdata->FFTLEN * 2, &ttp, NULL);
					}
				}
				else if ((delay_count == 8 && k != 0 && j != 0 && j <= 3 * (gwdata->FFTLEN / 2 / delay_count)) ||
					 (delay_count == 12 && k != 0 && j != 0 && j <= 5 * (gwdata->FFTLEN / 2 / delay_count)))
					ttp = gwfft_weight_over_weight (gwdata->dd_data, grp, grp - k);
				else
					ttp = gwfft_weight (gwdata->dd_data, grp);
				*weights++ = ttp;
			}
		    }
		}
	    }

/* Second build of the tables.  The inverse group multipliers must be rebuilt in the same order as used the normalization code. */

	    for (i = 0; i < gwdata->FFTLEN / 2; i += 8 * upper_avx512_word) {
		for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
		    for (avx512_word = 0; avx512_word < 8 * upper_avx512_word; avx512_word += upper_avx512_word) {
			double	ttmp;

/* For zero-padded and rational FFTs the upper half of the FFT has the exact same multipliers as the lower half. */
/* Thus we can cut the size of the inverse group multiplier table in half. */

			if ((gwdata->ZERO_PADDED_FFT || gwdata->RATIONAL_FFT) && k) continue;

/* Call quad-precision routine to compute group multipliers.  For two-pass negacyclic FFTs, we save a multiply by */
/* precomputing the group multiplier times the sine value of the roots-of-minus-one premultiplier. */

			grp = i + k + avx512_word;
			if (gwdata->PASS1_SIZE && gwdata->NEGACYCLIC_FFT) {
				/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one */
				/* premultiplier is for 2N, and a root-of-minus-one-of-2N is the same as */
				/* a root unity for 4N (where N is the number of complex values = FFTLEN/2). */
				unsigned long sine_word = i + avx512_word;
				gwfft_weights_times_sine (gwdata->dd_data, grp, sine_word, gwdata->FFTLEN * 2, NULL, &ttmp);
			} else
				ttmp = gwfft_weight_inverse (gwdata->dd_data, grp);
			*inverse_weights++ = ttmp;
		    }
		}
	    }
	}

/* Return pointer to end of allocated area */

	return (table);
}

/* Build the fudge factor flags table for an AVX-512 r4dwpn (radix-8 with partial normalization) FFT. */

double *zr4dwpn_build_fudge_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	uint64_t *compressed_fudges, *xor_masks, mask, mask_bit;
	unsigned long num_grp_mults, delay_count, upper_avx512_word, num_fudges, varblock_fudge_data_size;
	unsigned long group, h, i, j, k, avx512_word;
	unsigned char *p;

/* Initialize.  There are remarkable similarities between the one-pass and two-pass table builds. */

	if (gwdata->PASS1_SIZE == 0) {
		delay_count = 1;
		upper_avx512_word = 8;
		// Calculate number of fudge bytes written to each variable data block
		varblock_fudge_data_size = gwdata->FFTLEN / 8 * 7 / 8;
	} else {
		delay_count = zr4dwpn_delay_count (gwdata);
		upper_avx512_word = gwdata->PASS2_SIZE;
		num_grp_mults = gwdata->PASS1_SIZE;
		// Calculate number of fudge bytes written to each variable data block
		varblock_fudge_data_size = num_grp_mults * gwdata->PASS1_CACHE_LINES / 64;
	}

/* Let's describe what we are doing by way of an example.  Imagine the first 4 group multipliers (AVX-256 code) are as follows: */
/*		0.0	0.3535	0.707	0.06 */

/* There are 4 norm_grp_mults starting values of interest (where the gwfft_weight_exponent has "wrapped" from 0.9999 to 0.0: */
/*		(0.0	0.3535	0.707	0.06)  + 0.0 */
/*		(0.0	0.3535	0.707	0.06)  + 0.293 */
/*		(0.0	0.3535	0.707	0.06)  + 0.6465 */
/*		(0.0	0.3535	0.707	0.06)  + 0.94 */

/* Fudge factor flags simply denote when the group's gwfft_weight_exponent plus the column's gwfft_weight_exponent exceeds 1.0. */
/* There are 5 possible fudge factors for the group multipliers depending on the column weights: */
/* MASKSET0: 0000, 0010, 0110, 0111, 1111	for (0.0  0.3535  0.707  0.06) + 0.0 */
/* MASKSET1: 0000, 0100, 0101, 1101, 1111	for (0.0  0.3535  0.707  0.06) + 0.293 */
/* MASKSET2: 0000, 0001, 1001, 1011, 1111	for (0.0  0.3535  0.707  0.06) + 0.6465 */
/* MASKSET3: 0000, 1000, 1010, 1110, 1111	for (0.0  0.3535  0.707  0.06) + 0.94 */

/* To see how we can generate all the mask values using just eight 4-bit masks and one XOR mask for each group: */
/* MASKSET0: 0000, 0010, 0110, 0111, 1111  1101  1001  1000	(the eight 4-bit masks, XOR mask is 0000 */
/* MASKSET1:       0000, 0100, 0101, 1101, 1111			(XOR mask is 0010 applied to MASKSET0 above) */
/* MASKSET2:	         0000, 0001, 1001, 1011, 1111		(XOR mask is 0110 applied to MASKSET0 above) */
/* MASKSET3:		       0000, 1000, 1010, 1110, 1111	(XOR mask is 0111 applied to MASKSET0 above) */
/* Also note that the XOR masks appear in the eight 4-bit masks. */

/* Extending the above to 8 different AVX-512 words, we get 64-bit masks.  We'11 have 128 64-bit masks (1KB). */
/* Every 8 FFT words needs one 7-bit index into this array of masks, resulting in a 8:1 compression of the fudge */
/* flags (64-bit mask reduced to one byte index). */

/* Rather than having a separate table for the XOR masks, we note that there really aren't that many XOR masks to store. */
/* For a pass 1 size of 1280, there are only 1280/64 = 20 XOR masks.  So we simply make the first 20 entries in our */
/* 128 64-bit masks be the XOR masks in the order the assembly code will need them. */

	// Fudge flag compression only takes place for two-pass FFTs
	if (gwdata->PASS1_SIZE) {
		double	*norm_grp_mults;

		// Reserve space for 128 masks and up to 64 xor masks (xor masks could be dups)
		ASSERTG (num_grp_mults <= 4096);  // Not sure what would happen if there are more than 64 XOR masks
		asm_data->compressed_fudges = table;
		table = (double *) ((char *) table + 192 * sizeof (uint64_t));

/* Output the XOR masks by comparing all the norm_grp_mults to the first 64 norm_grp_mults. */

		// Create the XOR masks
		compressed_fudges = (uint64_t *) asm_data->compressed_fudges;
		memset (compressed_fudges, 0, num_grp_mults / 8);	// One bit used per grp_mult
		norm_grp_mults = (double *) asm_data->norm_grp_mults;
		for (i = 0; i < num_grp_mults; i++) if (norm_grp_mults[i] < norm_grp_mults[i&63]) bitset (compressed_fudges, i);
		num_fudges = num_grp_mults / 64;
	}

/* Calculate the group multiplier fudge flags. The fudge flag is set if the col mult * the grp mult */
/* is b times the correct fft_weight, meaning a mul by 1/b is required to generate the correct multiplier. */
/* Since we can't do equality compares on floats, this test is a little bit cryptic. */

/* Loop through all the pass 1 blocks in the same order the assembly code will process the groups. */
/* For each pass 1 block, output the proper number of bytes to the variable sin/cos data. */

	// Calculate address of fudge data in the first variable block data.
	// This is the first block's big/lit data minus the fudge flags size.
	p = (unsigned char *) gwdata->pass1_var_data + gwdata->biglit_data_offset - varblock_fudge_data_size;

	mask = 0;
	mask_bit = 1;
	for (group = 0; group < upper_avx512_word; group += gwdata->PASS1_CACHE_LINES) {
	    for (h = 0, xor_masks = compressed_fudges;
		 h < gwdata->FFTLEN / 2 / delay_count;
		 h += 8 * upper_avx512_word, xor_masks += delay_count / 4) {
		for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++, xor_masks -= delay_count / 4) {
		    unsigned long col;
		    double col_weight_exponent;
		    col = group + i;
		    col_weight_exponent = gwfft_weight_exponent (gwdata->dd_data, col);
		    // Generate delay_count/4 64-bit masks each with a different XOR mask.  This corresponds
		    // to one execution of zr8/zr12 first/last fft macro.
		    for (j = 0; j < gwdata->FFTLEN / 2; j += gwdata->FFTLEN / 2 / delay_count) {
			for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
			    for (avx512_word = 0; avx512_word < 8 * upper_avx512_word; avx512_word += upper_avx512_word) { // Build one byte
				unsigned long grp;
				double	grp_weight_exponent;
				grp = h + j + k + avx512_word;
				grp_weight_exponent = gwfft_weight_exponent (gwdata->dd_data, grp);
				if (gwfft_weight_exponent (gwdata->dd_data, grp + col) + 0.5 < grp_weight_exponent + col_weight_exponent)
					mask |= mask_bit;
				mask_bit <<= 1;
			    }
			    // One-pass FFTs do not compress the fudge flags
			    if (gwdata->PASS1_SIZE == 0) {
				// The first col multiplier is always 1.0 and the one-pass wrapper takes advantage of this.
				// Do not generate fudge flags when col = 0.  Otherwise, output the uncompressed fudge flags.
				if (col != 0) *p++ = (unsigned char) mask;
			    }
			    // Two-pass FFTs compress the fudge flags using the XOR mask and index into the compressed fudges array
			    else {
				int	index;
				if (mask_bit) continue;	// Still building the 64-bit mask
				// Apply the XOR mask
				mask ^= *xor_masks++;
				// Convert the mask to an index into the compressed fudges array
				for (index = 0; ; index++) {
					if (index == num_fudges) {
						compressed_fudges[num_fudges++] = mask;
						ASSERTG (num_fudges < 192);//BUG
						break;
					}
					if (compressed_fudges[index] == mask) break;
				}
				// Output the index into the compressed fudges array
				*p++ = (unsigned char) index;
			    }
			    // Init for building next mask
			    mask = 0;
			    mask_bit = 1;
			}
		    }
		}
	    }
	    // Calculate address of next pass 1 block's fudge flags
	    p += gwdata->pass1_var_data_size - varblock_fudge_data_size;
	}

/* Rebuild the group multipliers table.  For negacyclic FFTs, when first built we did not apply the roots-of-minus-one sine */
/* values so that we could build the xor table.  During the rebuild we will apply the sine value.  For real FFTs, we need to */
/* build the inverse group multipliers in the same order that normalize uses them. */

	(void) zr4dwpn_build_norm_table (gwdata, NULL);

/* Return pointer to end of our table */

	return (table);
}


/*************************************************************/
/*                      AVX FFT tables                       */
/*************************************************************/


/* This routine builds the sin/cos table used in a one pass AVX traditional DJB radix-4 FFT  - called by gwsetup. */
/* If this is a negacyclic FFT, then the root-of-minus-1 premultipliers are also built. */

double *yr4_build_onepass_sincos_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long size, avx_increment, j, N, temp;
	int	pow2_count;

/* Count the power-of-two FFT levels after the initial FFT levels.  If odd, the */
/* the last levels will be a radix-8, if even the last levels will be a radix-4. */

	size = gwdata->FFTLEN / 8;
	for (pow2_count = 0; (size & 1) == 0; size /= 2) pow2_count++;

/* Init necessary variables */

	size = gwdata->FFTLEN / 8;		/* Complex values we're generating sin/cos data for */
	avx_increment = gwdata->FFTLEN / 4;

/* The first group has no sin/cos data! */

	if (pow2_count & 1) {
		N = 8;
		size /= 8;
	} else {
		N = 4;
		size /= 4;
	}

/* For the yr4_four_complex_djbfft and yr4_eight_reals_four_complex_djbfft */
/* building block levels, output the sin/cos values. */

	if ((size & 3) == 0) {
		while ((size & 3) == 0) {
			N = N * 4;
			size /= 4;
		}

/* For the yr4_eight_reals_four_complex_djbfft building block levels, output the */
/* sin/cos values needed.  The eight_reals doubles N because the real part of the FFT */
/* is one level behind the complex part of the FFT.  The four-complex sin/cos values */
/* are the same for all 3 of the upper YMM doubles. */		

		if (!gwdata->NEGACYCLIC_FFT) {
			for (j = 0; j < N / 4; j++) {
				gwsincos125by4 (j, N*2, table);				/* For the eight_reals */
				gwsincos12by4 (j + avx_increment, N, table+1);		/* For the four-complex */
				table[3] = table[2] = table[1];
				table[7] = table[6] = table[5];
				table[11] = table[10] = table[9];
				table[15] = table[14] = table[13];
				table[19] = table[18] = table[17] = -table[1];
				table[23] = table[22] = table[21] = -table[5];
				table += 24;
			}
		}

/* Output the sin/cos values for negacyclic FFTs, used by the yr4__b4cl_four_complex_djbfft macro. */
/* We only need one sin/cos value pair as we use the vbroadcastsd instruction to fill out the YMM register. */

		else {
			for (j = 0; j < N / 4; j++) {
				gwsincos12by1 (j, N, table);
				table += 4;
			}
		}
	}

/* For the yr5_five_complex_djbfft building block levels, output the sin/cos values. */

	while (size % 5 == 0) {
		while ((size % 5) == 0) {
			N = N * 5;
			size /= 5;
		}

/* For the yr5_ten_reals_five_complex_djbfft building block levels, output the */
/* sin/cos values needed.  The ten_reals doubles N because the real part of the FFT */
/* is one level behind the complex part of the FFT.  The five-complex sin/cos values */
/* are the same for all 3 of the upper YMM doubles. */

		if (!gwdata->NEGACYCLIC_FFT) {
			for (j = 0; j < N / 5; j++) {
				gwsincos13by4 (j, N*2, table);				/* For the ten_reals */
				gwsincos12by4 (j + avx_increment, N, table+1);		/* For the five-complex */
				table[16] = table[8];					/* For the ten_reals */
				table[20] = table[12];
				table[8] = table[1];
				table[12] = table[5];
				table[24] = table[9];
				table[28] = table[13];
				table[3] = table[2] = table[1];				/* For the five-complex */
				table[7] = table[6] = table[5];
				table[11] = table[10] = table[9];
				table[15] = table[14] = table[13];
				table[19] = table[18] = table[17] = -table[9];
				table[23] = table[22] = table[21] = -table[13];
				table[27] = table[26] = table[25] = -table[1];
				table[31] = table[30] = table[29] = -table[5];
				table += 32;
			}
		}

/* Output the sin/cos data for the complex sections (used by the yr5_five_complex_djbfft building block). */

		else {
			for (j = 0; j < N / 5; j++) {
				gwsincos12by1 (j, N, table);
				table += 4;
			}
		}
	}

/* For the yr3_three_complex_djbfft building block levels, output the sin/cos values. */

	while (size % 3 == 0) {
		while ((size % 3) == 0) {
			N = N * 3;
			size /= 3;
		}

/* The yr3_six_reals building blocks require an extra sin/cos */
/* value.  The six_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

		if (!gwdata->NEGACYCLIC_FFT) {
			for (j = 0; j < N / 3; j++) {
				gwsincos12by4 (j, N*2, table);				/* For the six-reals FFT */
				gwsincos1by4 (j + avx_increment, N, table+1);		/* For the three-complex FFT */
				table[3] = table[2] = table[1];
				table[7] = table[6] = table[5];
				table[11] = table[10] = table[9] = -table[1];
				table[15] = table[14] = table[13] = -table[5];
				table += 16;
			}
		}

/* Output the sin/cos data for the complex sections (used by the yr3_three_complex_djbfft building block). */

		else {
			for (j = 0; j < N / 3; j++) {
				gwsincos1by1 (j, N, table);
				table += 2;
			}
		}
	}
	ASSERTG (size == 1);
	if (size != 1) gwdata->GWERROR = GWERROR_INTERNAL + 1;

/* Real FFTs output one last set of sin/cos values for the first 8-reals FFT. */

	avx_increment = gwdata->FFTLEN / 32;
	if (!gwdata->NEGACYCLIC_FFT) {
		N = gwdata->FFTLEN;
		for (j = 0; j < avx_increment; j++) {
			gwsincos125by4 (j, N, table);
			gwsincos125by4 (j + avx_increment, N, table+1);
			gwsincos125by4 (j + 2*avx_increment, N, table+2);
			gwsincos125by4 (j + 3*avx_increment, N, table+3);
			table += 24;
		}
	}

/* For negacyclic FFTs, the first FFT level converts from real values to complex */
/* values by multiplying by a root of -1 weight and doing a butterfly.  This is */
/* simplified because weights in the bottom half are sqrt(-1) times the */
/* matching weights in the upper half.  Thus, we butterfly upper_word * weight */
/* with bottom_word * i * weight.  That equals (upper_word + i * lower_word) * weight */
/* That is just a complex multiply with the half of the butterfly output values */
/* unneeded thanks to Hermetian symmetry. */

/* The second and third levels do a standard radix-4 four-complex-FFT */
/* building block with a post-multiply by a sin/cos root of unity. */

	else {
		N = gwdata->FFTLEN / 2;
		for (j = 0; j < avx_increment; j++) {

/* Here we compute the standard 0,1,2,3 * temp for the radix-4 sin/cos multipliers. */
/* Then we multiply in the roots-of-minus-one premultiplier.  The root-of-minus-one */
/* premultiplier was for 2N, and a root-of-minus-one-of-2N is the same as a root */
/* unity for 4N. */

			temp = j;
			gwsincos1plus0123by4 (temp, 4 * temp, N * 4, table); /* premult + temp*0-3 */
			temp += avx_increment;
			gwsincos1plus0123by4 (temp, 4 * temp, N * 4, table+1);
			temp += avx_increment;
			gwsincos1plus0123by4 (temp, 4 * temp, N * 4, table+2);
			temp += avx_increment;
			gwsincos1plus0123by4 (temp, 4 * temp, N * 4, table+3);
			table += 32;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds a big/little flags table - used by one-pass AVX normalization */
/* routines */

double *yr4_build_onepass_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned char *p = (unsigned char *) table;
	unsigned long i, j, top5bits;

/* Init table of first 8 big/lit values */

	memset (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES, 0, sizeof (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES));

/* Loop to build table for zero-padded case.  We only need half as much data */
/* because the upper half data are the same as the lower half data. */

	if (gwdata->ZERO_PADDED_FFT) {
		memset (p, 0, gwdata->FFTLEN / 8);
		for (i = 0; i < gwdata->FFTLEN / 2; i++) {

/* Only big words result in a bit being set in the biglit table */

			if (! is_big_word (gwdata, i)) continue;

/* Find where this data appears in the table we are building.  Use the same algorithm */
/* as addr_offset except no padding is necessary. */			

			top5bits = i / (gwdata->FFTLEN >> 5); j = i - top5bits * (gwdata->FFTLEN >> 5);
			j = ((top5bits >> 2) & 3) * (gwdata->FFTLEN >> 2) + (j << 3) + ((top5bits >> 4) << 2) + (top5bits & 3);

/* Add to the biglit table entry for each big word double in an AVX word */

			p[j >> 3] += 16 << (j & 3);

/* Create separate table for first 8 biglit values for carry propagation code */

			if (i < sizeof (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES))
				asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES[i] = 16;
		}

/* Return pointer past the end of the biglit table */

		return ((double *) (p + gwdata->FFTLEN / 8));
	}

/* Loop to build table for the non-zero-padded case */

	memset (p, 0, gwdata->FFTLEN / 4);
	for (i = 0; i < gwdata->FFTLEN; i++) {

/* Only big words result in a bit being set in the biglit table */

		if (! is_big_word (gwdata, i)) continue;

/* Find where this data appears in the table we are building.  Use the same algorithm */
/* as addr_offset except no padding is necessary. */			

		top5bits = i / (gwdata->FFTLEN >> 5); j = i - top5bits * (gwdata->FFTLEN >> 5);
		j = ((top5bits >> 2) & 3) * (gwdata->FFTLEN >> 2) + (j << 3) + ((top5bits >> 4) << 2) + (top5bits & 3);

/* Add to the biglit table entry for each big word double in an AVX word */

		p[j >> 2] += 16 << (j & 3);

/* Create separate table for first 8 biglit values for carry propagation code */

		if (i < sizeof (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES))
			asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES[i] = 16;
	}

/* Return pointer past the end of the biglit table */

	return ((double *) (p + gwdata->FFTLEN / 4));
}

/* This routine builds a normalization table - used by one-pass AVX radix-4 */
/* normalization routines. */

double *yr4_build_onepass_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned long i;
	intptr_t first_8_offsets[8];

/* Loop to build table for zero-padded case.  We only need half as much data */
/* because the upper half data are the same as the lower half data. */

	if (gwdata->ZERO_PADDED_FFT) {
		for (i = 0; i < gwdata->FFTLEN / 2; i++) {
			double	ttp, ttmp;
			unsigned long top5bits, j, table_entry;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Find where this data appears in the table we are building.  Use the same algorithm */
/* as addr_offset except no padding is necessary. */			

			top5bits = i / (gwdata->FFTLEN >> 5); j = i - top5bits * (gwdata->FFTLEN >> 5);
			j = ((top5bits >> 2) & 3) * (gwdata->FFTLEN >> 2) + (j << 3) + ((top5bits >> 4) << 2) + (top5bits & 3);
			table_entry = j >> 3;

/* Now set the entry for the proper double in an AVX word */

			table[table_entry*8+(j&3)] = ttmp;
			table[table_entry*8+4+(j&3)] = ttp;

/* Get offsets for carry propagation code to step through norm array */

			if (i <= 7) first_8_offsets[i] = (&table[table_entry*8+(j&3)] - table) * sizeof (double);
		}

/* Form pointer for next table */

		table += gwdata->FFTLEN;
	}

/* Loop to build table for non-zero-padded case */

	else {
		for (i = 0; i < gwdata->FFTLEN; i++) {
			double	ttp, ttmp;
			unsigned long top5bits, j, table_entry;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Find where this data appears in the table we are building.  Use the same algorithm */
/* as addr_offset except no padding is necessary. */			

			top5bits = i / (gwdata->FFTLEN >> 5); j = i - top5bits * (gwdata->FFTLEN >> 5);
			j = ((top5bits >> 2) & 3) * (gwdata->FFTLEN >> 2) + (j << 3) + ((top5bits >> 4) << 2) + (top5bits & 3);
			table_entry = j >> 2;

/* Now set the entry for the proper double in an AVX word */

			table[table_entry*8+(j&3)] = ttmp;
			table[table_entry*8+4+(j&3)] = ttp;

/* Get offsets for carry propagation code to step through norm array */

			if (i <= 7) first_8_offsets[i] = (&table[table_entry*8+(j&3)] - table) * sizeof (double);
		}

/* Create pointer for next table */

		table += gwdata->FFTLEN * 2;
	}

/* Make differences for carry propagation code to step through norm array */

	asm_data->u.ymm.YMM_NORM_INCR7 = first_8_offsets[7] - first_8_offsets[6];
	asm_data->u.ymm.YMM_NORM_INCR6 = first_8_offsets[6] - first_8_offsets[5];
	asm_data->u.ymm.YMM_NORM_INCR5 = first_8_offsets[5] - first_8_offsets[4];
	asm_data->u.ymm.YMM_NORM_INCR4 = first_8_offsets[4] - first_8_offsets[3];
	asm_data->u.ymm.YMM_NORM_INCR3 = first_8_offsets[3] - first_8_offsets[2];
	asm_data->u.ymm.YMM_NORM_INCR2 = first_8_offsets[2] - first_8_offsets[1];
	asm_data->u.ymm.YMM_NORM_INCR1 = first_8_offsets[1] - first_8_offsets[0];

/* Return pointer for next table */

	return (table);
}

/* This routine builds the sin/cos table used in pass 1 by the radix-4/8 DJB */
/* FFT with delayed sin/cos multiplies and with partial normalization. */

double *yr4dwpn_build_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned long pass1_size, pass1_increment, delay_count;
	unsigned long group, i, j, k, N, temp, upper_avx_word;
	int	pow2_count;
	int	wpn4 = FALSE;		/* Flag indicating we are using wpn4 in pass 1 */

/* Initialize some needed constants */

	pass1_size = gwdata->PASS1_SIZE;
	upper_avx_word = gwdata->PASS2_SIZE;
	pass1_increment = gwdata->PASS2_SIZE * 4;

/* Determine number of delay groups.  In a standard radix-4 FFT, there is only one sin/cos */
/* group in the last pass 1 level.  We reduce our memory usage by using a fixed sin/cos */
/* table in the first FFT levels and having multiple groups of sin/cos data in the last pass 1 level. */
/* I call these groups of sin/cos data in the last pass 1 level "delay groups". */

#ifdef USE_REDUCED_SINCOS_FFTS
	if (pass1_size == 1792 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 56;
	else if (pass1_size % 7 == 0)
		delay_count = 14;
	else if (pass1_size == 384 || pass1_size == 768)
		delay_count = 12;
	else if ((pass1_size == 640 && gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1280 && gwdata->NEGACYCLIC_FFT))
		delay_count = 20;
	else if (pass1_size == 1280 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 40;
	else if (pass1_size == 1536 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 48;
	else if (pass1_size % 5 == 0 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 10;
	else if ((pass1_size == 256 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 512 && !gwdata->NEGACYCLIC_FFT))
		delay_count = 8;
	else if ((pass1_size == 512 && gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1536 && gwdata->NEGACYCLIC_FFT) ||
		 pass1_size == 1024 ||
		 pass1_size == 2048)
		delay_count = 16;
	else
		delay_count = 4;
					// To do: convert pass 1 sizes above 1792
#else
	if (pass1_size % 7 == 0)
		delay_count = 14;
	else if ((pass1_size == 384 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 768 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1536 && !gwdata->NEGACYCLIC_FFT))
		delay_count = 12;
	else if (pass1_size % 5 == 0 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 10;
	else if (pass1_size == 256 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 8;
	else if ((pass1_size == 512 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1024 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1536 && gwdata->NEGACYCLIC_FFT) ||
		 pass1_size == 2048)
		delay_count = 16;
	else
		delay_count = 4;
#endif

/* Count the power-of-two FFT levels after the initial FFT levels.  If odd, the */
/* the last levels will be a radix-8, if even the last levels will be a radix-4. */

	pass1_size /= (delay_count * 2);
	for (pow2_count = 0; (pass1_size & 1) == 0; pass1_size /= 2) pow2_count++;

/* Set count of pass 1 blocks that share one set of two-to-phi grp multipliers */

#ifdef USE_REDUCED_SINCOS_FFTS
	if (pow2_count & 1) gwdata->wpn_count = 8;
	else if (gwdata->PASS1_SIZE == 2048) gwdata->wpn_count = 16;
	else gwdata->wpn_count = 4;
#else
	if (pow2_count & 1) gwdata->wpn_count = 8;
	else if ((gwdata->PASS1_SIZE == 1536 && !gwdata->NEGACYCLIC_FFT) ||
		 gwdata->PASS1_SIZE == 1792 ||
		 gwdata->PASS1_SIZE == 2048) gwdata->wpn_count = 16;
	else gwdata->wpn_count = 4;
#endif
#ifdef USE_WPN4
	{
		gwdata->wpn_count *= 4;
		wpn4 = TRUE;
	}
#endif

/* Set counters for inorm, zpnorm and ygw_carries to use.  Remember that ygw_carries */
/* always works on data after it has been copied to the scratch area. */

	asm_data->count2 = gwdata->wpn_count / 4;
	asm_data->count3 = asm_data->addcount1 / asm_data->count2;
	if (asm_data->count2 == 1) {
		asm_data->count4 = 1;
		asm_data->count5 = asm_data->count3 / 2;
	} else {
		asm_data->count4 = asm_data->count2 / 2;
		asm_data->count5 = asm_data->count3;
	}

/* Set pointer to table of multipliers */

	gwdata->pass1_var_data = table;

/* Loop through all the pass 1 groups in the same order the assembly code will */
/* process the groups. */

	for (group = 0; group < upper_avx_word; group += gwdata->PASS1_CACHE_LINES) {

		pass1_size = gwdata->PASS1_SIZE;
		pass1_size /= (delay_count * 2);	/* Complex values we're generating sin/cos data for */
		N = gwdata->PASS2_SIZE;

/* Output the sin/cos/premultiplier values for the radix-8 block that does the */
/* last 3 levels in pass 1.  NOTE:  We do not need the "j loop" (it would loop */
/* from zero to zero) when generating the sin/cos twiddle factors for the last */
/* levels of pass 1. */

#ifdef USE_REDUCED_SINCOS_FFTS
		if (pow2_count & 1) {
			N = N * 8;

/* Output the complex sin/cos values needed for a standard yr8_8cl_eight_complex_djbfft */
/* on the last pass 1 level.  At runtime, we compute the actual sin/cos values from this. */

			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
				// Asm code swizzled the input so that upper_avx_word is 1
				temp = group + i;
				gwsincos1234by4_raw (temp, N, table);
				gwsincos1234by4_raw (temp + 1, N, table+1);
				gwsincos1234by4_raw (temp + 2, N, table+2);
				gwsincos1234by4_raw (temp + 3, N, table+3);
				table += 32;
			}

/* For the yr8_sg8cl_sixteen_reals_fft8 building block, output the extra */
/* sin/cos values needed for the sixteen_reals. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					// Asm code swizzled the input so that upper_avx_word is 1
					temp = group + i;
					gwsincos159Dby4 (temp, N*2, table);
					gwsincos159Dby4 (temp + 1, N*2, table+1);
					gwsincos159Dby4 (temp + 2, N*2, table+2);
					gwsincos159Dby4 (temp + 3, N*2, table+3);
					table += 32;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the yr8_rsc_sg8cl_eight_complex_fft8 macro. */

			for (k = 0; k < delay_count; k++) {
				if (k == 0 && !gwdata->NEGACYCLIC_FFT) continue;
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					unsigned long bigN, ktemp, actemp, avx_word;

/* Work on each AVX word.  Unlike the SSE2 build-table code, we must recalculate */
/* ktemp for each AVX word because the ASM code swizzles its inputs */

					for (avx_word = 0; avx_word < 4; avx_word++) {
						unsigned long final_group = group + i + avx_word;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply part of the negacyclic premultiplier here. */

						if (gwdata->NEGACYCLIC_FFT) {
							bigN = gwdata->FFTLEN * 2;
							actemp = final_group;
						} else {
							bigN = gwdata->FFTLEN;
							actemp = 0;
						}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

						if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if (k == 1)
								ktemp = 2 * final_group * 4;
							else if (k == 2)
								ktemp = 1 * final_group * 4;
							else
								ktemp = bigN - 1 * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT && delay_count == 12) {
							/* 0,2,1,-1 combined with 0,1,-1 */
							int	kmap[12] = {0,4,-4, 2,6,-2, 1,5,-3, -1,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT && delay_count == 20) {
							/* 0,2,1,-1 combined with 0,1,2,-2,-1 */
							int	kmap[20] = {0,4,8,-8,-4, 2,6,10,-6,-2, 1,5,9,-7,-3, -1,3,7,-9,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT) {
							/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4, 2,10,6,-2, 1,9,5,-3, -1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if (k == 1)
								ktemp = 2 * final_group;
							else if (k == 2)
								ktemp = 1 * final_group;
							else
								ktemp = 5 * final_group;
						} else if (delay_count == 16) {
							/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[16] = {0,8,4,20, 2,18,10,-6, 1,17,9,-7, 5,21,13,-3};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 32) {
							/* 0...7 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[32] = {0,16,8,40,    1,33,17,-15,  2,34,18,-14,
									    3,35,19,-13,  4,36,20,-12,  5,37,21,-11,
									    6,38,22,-10,  7,39,23,-9};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 40) {
							/* 0...9 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[40] = {0,20,10,50,   1,41,21,-19,  2,42,22,-18,
									    3,43,23,-17,  4,44,24,-16,  5,45,25,-15,
									    6,46,26,-14,  7,47,27,-13,  8,48,28,-12,  9,49,29,-11};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 48) {
							/* 0...11 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[48] = {0,24,12,60,   1,49,25,-23,  2,50,26,-22,
									    3,51,27,-21,  4,52,28,-20,  5,53,29,-19,
									    6,54,30,-18,  7,55,31,-17,  8,56,32,-16,
									    9,57,33,-15, 10,58,34,-14, 11,59,35,-13};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 56) {
							/* 0...13 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[56] = {0,28,14,70,   1,57,29,-27,  2,58,30,-26,
									    3,59,31,-25,  4,60,32,-24,  5,61,33,-23,
									    6,62,34,-22,  7,63,35,-21,  8,64,36,-20,
									    9,65,37,-19, 10,66,38,-18, 11,67,39,-17,
									   12,68,40,-16, 13,69,41,-15};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else {			/* delay_count == 8, 10, 12, or 14 */
							/* Multipliers for the radix-16, radix-20, radix-24, or radix-28 step */
							ktemp = k * final_group;
						}

/* We now calculate the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

						gwsincos1by4_raw (actemp + ktemp, bigN, table + avx_word);
					}
					table += 8;
				}
			}
			pass1_size /= 8;
		}

/* Output the sin/cos/premultiplier values for the radix-4 block that does the */
/* last 2 levels in pass 1. */

		else {
			N = N * 4;

/* Output the complex sin/cos values needed for a standard yr4_4cl_four_complex_djbfft */
/* on the last pass 1 level.  At runtime, we compute the actual sin/cos values from this. */

			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
				// Asm code swizzled the input so that upper_avx_word is 1
				temp = group + i;
				gwsincos12by4_raw (temp, N, table);
				gwsincos12by4_raw (temp + 1, N, table+1);
				gwsincos12by4_raw (temp + 2, N, table+2);
				gwsincos12by4_raw (temp + 3, N, table+3);
				table += 16;
			}

/* Output the extra sin/cos values needed for the eight_reals FFT work done */
/* on the last pass 1 level.  We double N because the real part of the FFT */
/* is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					// Asm code swizzled the input so that upper_avx_word is 1
					temp = group + i;
					gwsincos15by4 (temp, N*2, table);
					gwsincos15by4 (temp + 1, N*2, table+1);
					gwsincos15by4 (temp + 2, N*2, table+2);
					gwsincos15by4 (temp + 3, N*2, table+3);
					table += 16;
				}
			}

/* Output the sin/cos values for the delay groups -- specifically the yr4_rsc_sg4cl_four_complex_fft4 macro. */

			for (k = 0; k < delay_count; k++) {
				if (k == 0 && !gwdata->NEGACYCLIC_FFT) continue;
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					unsigned long bigN, ktemp, actemp, avx_word;

/* Work on each AVX word.  Unlike the SSE2 build-table code, we must recalculate */
/* ktemp for each AVX word because the ASM code swizzles its inputs */

					for (avx_word = 0; avx_word < 4; avx_word++) {
						unsigned long final_group = group + i + avx_word;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply part of the negacyclic premultiplier here. */

						if (gwdata->NEGACYCLIC_FFT) {
							bigN = gwdata->FFTLEN * 2;
							actemp = final_group;
						} else {
							bigN = gwdata->FFTLEN;
							actemp = 0;
						}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

						if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if	(k == 1)
								ktemp = 2 * final_group * 4;
							else if (k == 2)
								ktemp = 1 * final_group * 4;
							else
								ktemp = bigN - 1 * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT && delay_count == 12) {
							/* 0,2,1,-1 combined with 0,1,-1 */
							int	kmap[12] = {0,4,-4, 2,6,-2, 1,5,-3, -1,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT && delay_count == 20) {
							/* 0,2,1,-1 combined with 0,1,2,-2,-1 */
							int	kmap[20] = {0,4,8,-8,-4, 2,6,10,-6,-2, 1,5,9,-7,-3, -1,3,7,-9,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT) {
							/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4, 2,10,6,-2, 1,9,5,-3, -1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if	(k == 1)
								ktemp = 2 * final_group;
							else if (k == 2)
								ktemp = 1 * final_group;
							else
								ktemp = 5 * final_group;
						} else if (delay_count == 16) {
							/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[16] = {0,8,4,20, 2,18,10,-6, 1,17,9,-7, 5,21,13,-3};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 32) {
							/* 0...7 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[32] = {0,16,8,40,    1,33,17,-15,  2,34,18,-14,
									    3,35,19,-13,  4,36,20,-12,  5,37,21,-11,
									    6,38,22,-10,  7,39,23,-9};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 40) {
							/* 0...9 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[40] = {0,20,10,50,   1,41,21,-19,  2,42,22,-18,
									    3,43,23,-17,  4,44,24,-16,  5,45,25,-15,
									    6,46,26,-14,  7,47,27,-13,  8,48,28,-12,  9,49,29,-11};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 48) {
							/* 0...11 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[48] = {0,24,12,60,   1,49,25,-23,  2,50,26,-22,
									    3,51,27,-21,  4,52,28,-20,  5,53,29,-19,
									    6,54,30,-18,  7,55,31,-17,  8,56,32,-16,
									    9,57,33,-15, 10,58,34,-14, 11,59,35,-13};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else if (delay_count == 56) {
							/* 0...13 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[56] = {0,28,14,70,   1,57,29,-27,  2,58,30,-26,
									    3,59,31,-25,  4,60,32,-24,  5,61,33,-23,
									    6,62,34,-22,  7,63,35,-21,  8,64,36,-20,
									    9,65,37,-19, 10,66,38,-18, 11,67,39,-17,
									   12,68,40,-16, 13,69,41,-15};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else {			/* delay_count == 8, 10, 12, or 14 */
							/* Multipliers for the radix-16, radix-20, radix-24, or radix-28 step */
							ktemp = k * final_group;
						}

/* We now calculate the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

						gwsincos1by4_raw (actemp + ktemp, bigN, table + avx_word);
					}
					table += 8;
				}
			}
			pass1_size /= 4;
		}

/* Output the sin/cos/premultiplier values for the radix-8 block that does the */
/* last 3 levels in pass 1.  NOTE:  We do not need the "j loop" (it would loop */
/* from zero to zero) when generating the sin/cos twiddle factors for the last */
/* levels of pass 1. */

#else
		if (pow2_count & 1) {
			N = N * 8;

/* For the yr8_sg8cl_sixteen_reals_fft8 building block, output the extra */
/* sin/cos values needed for the sixteen_reals. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					// Asm code swizzled the input so that upper_avx_word is 1
					temp = group + i;
					gwsincos159Dby4 (temp, N*2, table);
					gwsincos159Dby4 (temp + 1, N*2, table+1);
					gwsincos159Dby4 (temp + 2, N*2, table+2);
					gwsincos159Dby4 (temp + 3, N*2, table+3);
					table += 32;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the yr8_sg8cl_eight_complex_fft8 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					unsigned long bigN, ktemp, actemp, avx_word;

/* Work on each AVX word.  Unlike the SSE2 build-table code, we must recalculate */
/* ktemp for each AVX word because the ASM code swizzles its inputs */

					for (avx_word = 0; avx_word < 4; avx_word++) {
						unsigned long final_group = group + i + avx_word;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply the part of the negacyclic premultiplier here. */

						if (gwdata->NEGACYCLIC_FFT) {
							bigN = gwdata->FFTLEN * 2;
							actemp = final_group;
						} else {
							bigN = gwdata->FFTLEN;
							actemp = 0;
						}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

						if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if (k == 1)
								ktemp = 2 * final_group * 4;
							else if (k == 2)
								ktemp = 1 * final_group * 4;
							else
								ktemp = bigN - 1 * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT) {
							/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if (k == 1)
								ktemp = 2 * final_group;
							else if (k == 2)
								ktemp = 1 * final_group;
							else
								ktemp = 5 * final_group;
						} else if (delay_count == 16) {
							/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else {			/* delay_count == 8, 10, 12, or 14 */
							/* Multipliers for the radix-16, radix-20, radix-24, or radix-28 step */
							ktemp = k * final_group;
						}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3,4,5,6,7 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

						temp = final_group * (bigN / N);
						gwsincos1plus01234567by4 (actemp + ktemp, temp, bigN, table + avx_word); /* premult,delay and temp*0-7 */
					}
					table += 64;
				}
			}
			pass1_size /= 8;
		}

/* Output the sin/cos/premultiplier values for the radix-4 block that does the */
/* last 2 levels in pass 1. */

		else {
			N = N * 4;

/* Output the extra sin/cos values needed for the eight_reals FFT work done */
/* on the last pass 1 level.  We double N because the real part of the FFT */
/* is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					// Asm code swizzled the input so that upper_avx_word is 1
					temp = group + i;
					gwsincos15by4 (temp, N*2, table);
					gwsincos15by4 (temp + 1, N*2, table+1);
					gwsincos15by4 (temp + 2, N*2, table+2);
					gwsincos15by4 (temp + 3, N*2, table+3);
					table += 16;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the yr4_sg4cl_four_complex_fft4 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i += 4) {
					unsigned long bigN, ktemp, actemp, avx_word;

/* Work on each AVX word.  Unlike the SSE2 build-table code, we must recalculate */
/* ktemp for each AVX word because the ASM code swizzles its inputs */

					for (avx_word = 0; avx_word < 4; avx_word++) {
						unsigned long final_group = group + i + avx_word;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply part of the negacyclic premultiplier here. */

						if (gwdata->NEGACYCLIC_FFT) {
							bigN = gwdata->FFTLEN * 2;
							actemp = final_group;
						} else {
							bigN = gwdata->FFTLEN;
							actemp = 0;
						}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

						if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if	(k == 1)
								ktemp = 2 * final_group * 4;
							else if (k == 2)
								ktemp = 1 * final_group * 4;
							else
								ktemp = bigN - 1 * final_group * 4;
						} else if (gwdata->NEGACYCLIC_FFT) {
							/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group * 4;
							else
								ktemp = bigN + kmap[k] * final_group * 4;
						} else if (delay_count == 4) {
							if (k == 0)
								ktemp = 0;
							else if	(k == 1)
								ktemp = 2 * final_group;
							else if (k == 2)
								ktemp = 1 * final_group;
							else
								ktemp = 5 * final_group;
						} else if (delay_count == 16) {
							/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
							int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * final_group;
							else
								ktemp = bigN + kmap[k] * final_group;
						} else {			/* delay_count == 8, 10, 12, or 14 */
							/* Multipliers for the radix-16, radix-20, radix-24, or radix-28 step */
							ktemp = k * final_group;
						}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

						temp = final_group * (bigN / N);
						gwsincos1plus0123by4 (actemp + ktemp, temp, bigN, table + avx_word); /* premult,delay and temp*0-3 */
					}
					table += 32;
				}
			}
			pass1_size /= 4;
		}
#endif

/* Output multipliers for the four complex building blocks. */

		while ((pass1_size & 3) == 0) {

			N = N * 4;

/* For the wpn4 building block level, output a separate table of column normalization values before the sin/cos data. */

			if (wpn4 && N == gwdata->PASS2_SIZE * gwdata->wpn_count) {
				double *weights, *inv_weights;

/* The weights are output in separate tables before the sin/cos values.  This requires two registers */
/* to access the tables, but gains in that we can group data in cache lines better. */

				weights = table;
				table += N / pass1_increment * gwdata->PASS1_CACHE_LINES;
				inv_weights = table;
				table += N / pass1_increment * gwdata->PASS1_CACHE_LINES;

/* Output the weights before the sin/cos data, used by the yr4_4cl_wpn4_four_complex_djbfft macro. */
/* We apply the two-to-phi weight for the upper AVX words in the group multipliers.  There is a */
/* reason for doing it there rather than here (it reduces the number of valid fudge factor combinations */
/* for each AVX word from 16 to 5). */

				for (j = 0; j < N / 4; j += pass1_increment) {
				    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwfft_weights3 (gwdata->dd_data, temp, weights, NULL, inv_weights);
					gwfft_weights3 (gwdata->dd_data, temp + N/4, weights+1, NULL, inv_weights+1);
					gwfft_weights3 (gwdata->dd_data, temp + 2*N/4, weights+2, NULL, inv_weights+2);
					gwfft_weights3 (gwdata->dd_data, temp + 3*N/4, weights+3, NULL, inv_weights+3);
					weights += 4;
					inv_weights += 4;
				    }
				}
			}

/* For the non-wpn and wpn4 levels, output the sin/cos values. */

			if (wpn4 || N != gwdata->PASS2_SIZE * gwdata->wpn_count * 4) {

/* Output the sin/cos value for the complex sections, used by the yr4_4cl_four_complex_djbfft macro */

				for (j = 0; j < N / 4; j += pass1_increment) {
				    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by4 (temp, N, table);
					gwsincos12by4 (temp + upper_avx_word, N, table+1);
					gwsincos12by4 (temp + 2 * upper_avx_word, N, table+2);
					gwsincos12by4 (temp + 3 * upper_avx_word, N, table+3);
					table += 16;

/* For the yr4_4cl_csc_eight_reals_fft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos15by4 (temp, N*2, table);
						gwsincos15by4 (temp + upper_avx_word, N*2, table+1);
						gwsincos15by4 (temp + 2 * upper_avx_word, N*2, table+2);
						gwsincos15by4 (temp + 3 * upper_avx_word, N*2, table+3);
						table += 16;
					}

				    }
				}
			}

/* For the wpn building block level, output the sin/cos and column normalization values. */

			if (!wpn4 && N == gwdata->PASS2_SIZE * gwdata->wpn_count * 4) {
				double *weights, *inv_weights;

/* The weights are output in separate tables before the sin/cos values.  This requires two registers */
/* to access the tables, but gains in that we can group data in cache lines better. */

				weights = table;
				table += N / 4 / pass1_increment * gwdata->PASS1_CACHE_LINES;
				inv_weights = table;
				table += N / 4 / pass1_increment * gwdata->PASS1_CACHE_LINES;

/* Output the sin/cos value for the complex sections, used by the yr4_4cl_wpn_four_complex_djbfft macro */
/* We apply the two-to-phi weight for the upper AVX words in the group multipliers.  There is a */
/* reason for doing it there rather than here (it reduces the number of valid fudge factor combinations */
/* for each AVX word from 16 to 5). */

				for (j = 0; j < N / 4; j += pass1_increment) {
				    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos012by4_weighted (gwdata->dd_data, temp, upper_avx_word, N, temp, table);
					*weights++ = table[24];
					*inv_weights++ = table[25];
					table += 24;

/* For the yr4_4cl_csc_wpn_eight_reals_four_complex_djbfft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos15by4_weighted (gwdata->dd_data, temp, upper_avx_word, N*2, temp, table);
						table += 24;
					}
				    }
				}
			}

			pass1_size /= 4;
		}

/* For the yr5_five_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 5 == 0) {
			N = N * 5;

/* Output the sin/cos data for the complex sections, (the yr5_5cl_five_complex_djbfft building block). */

			for (j = 0; j < N / 5; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by4 (temp, N, table);
					gwsincos12by4 (temp + upper_avx_word, N, table+1);
					gwsincos12by4 (temp + 2 * upper_avx_word, N, table+2);
					gwsincos12by4 (temp + 3 * upper_avx_word, N, table+3);
					table += 16;

/* The yr5_5cl_csc_ten_reals building blocks require extra sin/cos values.  The ten_reals doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos13by4 (temp, N*2, table);
						gwsincos13by4 (temp + upper_avx_word, N*2, table+1);
						gwsincos13by4 (temp + 2 * upper_avx_word, N*2, table+2);
						gwsincos13by4 (temp + 3 * upper_avx_word, N*2, table+3);
						table += 16;
					}
				}
			}
			pass1_size /= 5;
		}

/* For the yr3_3cl_three_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 3 == 0) {
			N = N * 3;

/* Output the sin/cos data for the complex sections (used by the yr3_3cl_three_complex_djbfft building block). */

			for (j = 0; j < N / 3; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos1by4 (temp, N, table);
					gwsincos1by4 (temp + upper_avx_word, N, table+1);
					gwsincos1by4 (temp + 2 * upper_avx_word, N, table+2);
					gwsincos1by4 (temp + 3 * upper_avx_word, N, table+3);
					table += 8;

/* The yr3_3cl_csc_six_reals building blocks require an extra sin/cos value.  The six_reals doubles N */
/* because the real part of the FFT is one level behind the complex part of the FFT. */

					if (!gwdata->NEGACYCLIC_FFT) {
						gwsincos1by4 (temp, N*2, table);
						gwsincos1by4 (temp + upper_avx_word, N*2, table+1);
						gwsincos1by4 (temp + 2 * upper_avx_word, N*2, table+2);
						gwsincos1by4 (temp + 3 * upper_avx_word, N*2, table+3);
						table += 8;
					}
				}
			}
			pass1_size /= 3;
		}

/* Calculate the size of each group's sin/cos/premult data for pass1_get_next_block */

		if (group == 0) gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the fixed postmultiplier table used in pass 1 of the */
/* radix-4/8 delayed DJB FFT - called by gwsetup. */

double *yr4dwpn_build_fixed_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long upper_avx_word, pass1_increment, pass1_size, i, j, N;

/* Initialize some needed constants */

	upper_avx_word = gwdata->PASS2_SIZE;
	pass1_increment = 4 * gwdata->PASS2_SIZE;
	pass1_size = gwdata->PASS1_SIZE;

/* Real FFTs output one shared set of sin/cos values for the first 16-reals, 20-reals, 24-reals, 28-reals, or 8-reals FFT. */

	if (!gwdata->NEGACYCLIC_FFT) {
		N = gwdata->FFTLEN;
		if (pass1_size == 256 || pass1_size == 512) {
			for (j = 0; j < N / 16; j += pass1_increment) {
				for (i = 1; i <= 7; i++) {	/* Create 7 twiddle factors */
					gwsincos1by4 (i * j, N, table);
					gwsincos1by4 (i * (j + upper_avx_word), N, table+1);
					gwsincos1by4 (i * (j + 2 * upper_avx_word), N, table+2);
					gwsincos1by4 (i * (j + 3 * upper_avx_word), N, table+3);
					table += 8;
				}
			}
			N = N / 16;
		}
		else if (pass1_size % 20 == 0) {
			for (j = 0; j < N / 20; j += pass1_increment) {
				for (i = 1; i <= 9; i++) {	/* Create 9 twiddle factors */
					gwsincos1by4 (i * j, N, table);
					gwsincos1by4 (i * (j + upper_avx_word), N, table+1);
					gwsincos1by4 (i * (j + 2 * upper_avx_word), N, table+2);
					gwsincos1by4 (i * (j + 3 * upper_avx_word), N, table+3);
					table += 8;
				}
			}
			N = N / 20;
		}
		else if (pass1_size == 384 || pass1_size == 768 || pass1_size == 1536) {
			for (j = 0; j < N / 24; j += pass1_increment) {
				for (i = 1; i <= 11; i++) {	/* Create 11 twiddle factors */
					gwsincos1by4 (i * j, N, table);
					gwsincos1by4 (i * (j + upper_avx_word), N, table+1);
					gwsincos1by4 (i * (j + 2 * upper_avx_word), N, table+2);
					gwsincos1by4 (i * (j + 3 * upper_avx_word), N, table+3);
					table += 8;
				}
			}
			N = N / 24;
		}
		else if (pass1_size % 28 == 0) {
			for (j = 0; j < N / 28; j += pass1_increment) {
				for (i = 1; i <= 13; i++) {	/* Create 13 twiddle factors */
					gwsincos1by4 (i * j, N, table);
					gwsincos1by4 (i * (j + upper_avx_word), N, table+1);
					gwsincos1by4 (i * (j + 2 * upper_avx_word), N, table+2);
					gwsincos1by4 (i * (j + 3 * upper_avx_word), N, table+3);
					table += 8;
				}
			}
			N = N / 28;
		}
		else {
			for (j = 0; j < N / 8; j += pass1_increment) {
				gwsincos125by4 (j, N, table);
				gwsincos125by4 (j + upper_avx_word, N, table+1);
				gwsincos125by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos125by4 (j + 3 * upper_avx_word, N, table+3);
				table += 24;
			}
			N = N / 8;
		}
		/* Sometimes we also use a fixed sin/cos table for a four-complex in */
		/* the next FFT levels to further reduce memory usage. */
		if (pass1_size == 1024 ||
#ifdef USE_REDUCED_SINCOS_FFTS
		    pass1_size == 1280 ||
		    pass1_size == 1536 ||
		    pass1_size == 1792 ||
#endif
		    pass1_size == 2048) {
			/* Output the sin/cos values for the complex data followed by sin/cos values for the real data */
			for (j = 0; j < N / 4; j += pass1_increment) {
				gwsincos12by4 (j, N, table);
				gwsincos12by4 (j + upper_avx_word, N, table+1);
				gwsincos12by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos12by4 (j + 3 * upper_avx_word, N, table+3);
				table += 16;
				gwsincos15by4 (j, N*2, table);
				gwsincos15by4 (j + upper_avx_word, N*2, table+1);
				gwsincos15by4 (j + 2 * upper_avx_word, N*2, table+2);
				gwsincos15by4 (j + 3 * upper_avx_word, N*2, table+3);
				table += 16;
			}
			N = N / 4;
		}
	}

/* For negacyclic FFTs, build the fixed roots-of-minus-one table and the */
/* DJB FFT sin/cos table.  Output these values in the same order they will */
/* be used in the first two levels of pass 1. */

	else {
		N = gwdata->FFTLEN / 2;
		for (j = 0; j < N / 4; j += pass1_increment) {
			/* Compute the roots-of-minus-one premultiplier.  The root-of-minus-one */
			/* premultiplier is for 2N, and a root-of-minus-one-of-2N is the same as */
			/* a root unity for 4N. */
			gwsincos1plus0123by4 (j, N / 4, N * 4, table);
			gwsincos1plus0123by4 (j + upper_avx_word, N / 4, N * 4, table + 1);
			gwsincos1plus0123by4 (j + 2 * upper_avx_word, N / 4, N * 4, table + 2);
			gwsincos1plus0123by4 (j + 3 * upper_avx_word, N / 4, N * 4, table + 3);
			table += 32;
			/* Output the fixed sin/cos DJB FFT entry */
			gwsincos12by4 (j, N, table);
			gwsincos12by4 (j + upper_avx_word, N, table+1);
			gwsincos12by4 (j + 2 * upper_avx_word, N, table+2);
			gwsincos12by4 (j + 3 * upper_avx_word, N, table+3);
			table += 16;
		}
		N = N / 4;
		/* Sometimes we also use a fixed sin/cos table for */
		/* the next FFT levels to further reduce memory usage. */
#ifdef USE_REDUCED_SINCOS_FFTS
		if (pass1_size == 384 || pass1_size == 768) {
			for (j = 0; j < N / 3; j += pass1_increment) {
				gwsincos1by4 (j, N, table);
				gwsincos1by4 (j + upper_avx_word, N, table+1);
				gwsincos1by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos1by4 (j + 3 * upper_avx_word, N, table+3);
				table += 8;
			}
			N = N / 3;
		}
		if (pass1_size == 512 || pass1_size == 1024 || pass1_size == 1536 || pass1_size == 2048) {
			/* Output the sin/cos values for the complex data */
			for (j = 0; j < N / 4; j += pass1_increment) {
				gwsincos12by4 (j, N, table);
				gwsincos12by4 (j + upper_avx_word, N, table+1);
				gwsincos12by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos12by4 (j + 3 * upper_avx_word, N, table+3);
				table += 16;
			}
			N = N / 4;
		}
		if (pass1_size == 640 || pass1_size == 1280) {
			for (j = 0; j < N / 5; j += pass1_increment) {
				gwsincos12by4 (j, N, table);
				gwsincos12by4 (j + upper_avx_word, N, table+1);
				gwsincos12by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos12by4 (j + 3 * upper_avx_word, N, table+3);
				table += 16;
			}
			N = N / 5;
		}
#else
		if (pass1_size == 1536 || pass1_size == 2048) {
			/* Output the sin/cos values for the complex data */
			for (j = 0; j < N / 4; j += pass1_increment) {
				gwsincos12by4 (j, N, table);
				gwsincos12by4 (j + upper_avx_word, N, table+1);
				gwsincos12by4 (j + 2 * upper_avx_word, N, table+2);
				gwsincos12by4 (j + 3 * upper_avx_word, N, table+3);
				table += 16;
			}
			N = N / 4;
		}
#endif
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in complex pass 2 blocks in a traditional radix-4 FFT - called by gwsetup. */

double *yr4_build_pass2_complex_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, j, N;
	unsigned int pow2_count;

/* If the pass 2 size is divisible by 3, then the initial levels do */
/* radix-3 steps which only requires one sin/cos value.  The first levels */
/* in pass 2 have an upper_avx_word of one. */

	N = gwdata->PASS2_SIZE;
	while (N % 3 == 0) {
		for (i = 0; i < N / 3; i += 4) {
			gwsincos1by4 (i, N, table);
			gwsincos1by4 (i+1, N, table+1);
			gwsincos1by4 (i+2, N, table+2);
			gwsincos1by4 (i+3, N, table+3);
			table += 8;
		}
		N = N / 3;
	}

/* For initial level radix-5 building block, output two sin/cos values. */

	while (N % 5 == 0) {
		for (i = 0; i < N / 5; i += 4) {
			gwsincos12by4 (i, N, table);
			gwsincos12by4 (i+1, N, table+1);
			gwsincos12by4 (i+2, N, table+2);
			gwsincos12by4 (i+3, N, table+3);
			table += 16;
		}
		N = N / 5;
	}

/* For the first level radix-4 block, output two sin/cos values. */

	for (i = 0; i < N / 4; i += 4) {
		gwsincos12by4 (i, N, table);
		gwsincos12by4 (i+1, N, table+1);
		gwsincos12by4 (i+2, N, table+2);
		gwsincos12by4 (i+3, N, table+3);
		table += 16;
	}
	N = N / 4;

/* Build a smaller table for the remaining FFT levels */

	for (i = N, pow2_count = 0; (i & 1) == 0; i >>= 1) pow2_count++;
	if (pow2_count & 1) N = 128;		/* Last seven levels done in L1 cache */
	else if (N > 256) N = 256;		/* Last eight levels done in L1 cache */
	for (i = 0; i < 4; i++) {
		for (j = 0; j < N / 4; j += 4) {
			gwsincos12by1 (i + j, N, table);
			table += 4;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in pass 2 of real FFTs, */

double *yr4_build_pass2_real_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long avx_increment, j, N;

/* Negacyclic FFTs, don't need these tables */

	if (gwdata->NEGACYCLIC_FFT) return (table);

/* Init */

	avx_increment = 1;
	N = gwdata->PASS2_SIZE;

/* Output sin/cos values for an initial 6-real, 10-real, or 8-real macro. */

	while (N % 3 == 0) {
		for (j = 0; j < N / 3; j += 4) {
			gwsincos1by4 (j, N*2, table);		/* For the six real (and three complex) */
			gwsincos1by4 (j + avx_increment, N*2, table+1);
			gwsincos1by4 (j + 2*avx_increment, N*2, table+2);
			gwsincos1by4 (j + 3*avx_increment, N*2, table+3);
			table += 8;
		}
		N = N / 3;
	}

	while (N % 5 == 0) {
		for (j = 0; j < N / 5; j += 4) {
			gwsincos13by4 (j, N*2, table);	/* For the ten real (and five complex) */
			gwsincos13by4 (j + avx_increment, N*2, table+1);
			gwsincos13by4 (j + 2*avx_increment, N*2, table+2);
			gwsincos13by4 (j + 3*avx_increment, N*2, table+3);
			table += 16;
		}
		N = N / 5;
	}

	for (j = 0; j < N / 4; j += 4) {
		gwsincos15by4 (j, N*2, table);
		gwsincos15by4 (j + avx_increment, N*2, table+1);
		gwsincos15by4 (j + 2*avx_increment, N*2, table+2);
		gwsincos15by4 (j + 3*avx_increment, N*2, table+3);
		table += 16;
	}
	N = N / 4;

/* Output one last sin/cos table for the remaining yr4_eight_reals_four_complex_djbfft */
/* building block levels.  The eight_reals doubles N because the real part of the FFT */
/* is one level behind the complex part of the FFT.  The four-complex sin/cos values */
/* are the same for all 3 of the upper YMM doubles. */		

	avx_increment = N * 2;
	for (j = 0; j < N / 4; j++) {
		gwsincos125by4 (j, N*2, table);				/* For the eight_reals */
		gwsincos12by4 (j + avx_increment, N, table+1);		/* For the four-complex */
		table[3] = table[2] = table[1];
		table[7] = table[6] = table[5];
		table[11] = table[10] = table[9];
		table[15] = table[14] = table[13];
		table[19] = table[18] = table[17] = -table[1];
		table[23] = table[22] = table[21] = -table[5];
		table += 24;
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds a normalization table - used by radix-4 with partial */
/* normalization FFTs. */

double *yr4dwpn_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, j, num_cols, upper_avx_word;

/* Build the group multipliers table */

	upper_avx_word = gwdata->PASS2_SIZE;
	num_cols = gwdata->PASS2_SIZE * gwdata->wpn_count;
	for (i = 0; i < gwdata->FFTLEN / 2; i += num_cols) {
		for (j = 0; j < gwdata->FFTLEN; j += gwdata->FFTLEN / 2) {
			double	double1_weight, double2_weight, double3_weight, double4_weight;
			int	double1_sort, double2_sort, double3_sort, double4_sort;
			double	ttp, ttmp, ttp_over_b, ttmp_times_b, *tab20;

/* For zero-padded FFTs the upper half of the FFT has the exact same multipliers as the lower half. */
/* Thus we can cut the size of our group multiplier table in half. */

			if (gwdata->ZERO_PADDED_FFT && j >= gwdata->FFTLEN / 2) continue;

/* The sort order of the weights determines which fudge factor combination never occur. */

			double1_weight = gwfft_weight_exponent (gwdata->dd_data, i + j);
			double2_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + upper_avx_word);
			double3_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + 2 * upper_avx_word);
			double4_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + 3 * upper_avx_word);

/* Now sort the four weights */

			double1_sort = (double1_weight > double2_weight) +
				       (double1_weight > double3_weight) +
				       (double1_weight > double4_weight);
			double2_sort = (double2_weight > double1_weight) +
				       (double2_weight > double3_weight) +
				       (double2_weight > double4_weight);
			double3_sort = (double3_weight > double1_weight) +
				       (double3_weight > double2_weight) +
				       (double3_weight > double4_weight);
			double4_sort = (double4_weight > double1_weight) +
				       (double4_weight > double2_weight) +
				       (double4_weight > double3_weight);

/* Call quad-precision routine to compute set of multipliers. */
/* We compute two-to-phi multiplied by the fudge factor so that normalize won't have to. */

			gwfft_weights_fudged (gwdata->dd_data, i + j, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);

/* Set the LSW entries in an AVX word */

			table[0] = ttmp;
			table[4] = double1_sort < 3 ? ttmp : ttmp_times_b;
			table[8] = double1_sort < 2 ? ttmp : ttmp_times_b;
			table[12] = double1_sort < 1 ? ttmp : ttmp_times_b;
			table[16] = ttmp_times_b;
			tab20 = table + 20;
			tab20[0] = ttp;
			tab20[4] = double1_sort < 3 ? ttp : ttp_over_b;
			tab20[8] = double1_sort < 2 ? ttp : ttp_over_b;
			tab20[12] = double1_sort < 1 ? ttp : ttp_over_b;
			tab20[16] = ttp_over_b;

/* Set the remaining entries in an AVX word */

			gwfft_weights_fudged (gwdata->dd_data, i + j + upper_avx_word, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);
			table[1] = ttmp;
			table[5] = double2_sort < 3 ? ttmp : ttmp_times_b;
			table[9] = double2_sort < 2 ? ttmp : ttmp_times_b;
			table[13] = double2_sort < 1 ? ttmp : ttmp_times_b;
			table[17] = ttmp_times_b;
			tab20[1] = ttp;
			tab20[5] = double2_sort < 3 ? ttp : ttp_over_b;
			tab20[9] = double2_sort < 2 ? ttp : ttp_over_b;
			tab20[13] = double2_sort < 1 ? ttp : ttp_over_b;
			tab20[17] = ttp_over_b;

			gwfft_weights_fudged (gwdata->dd_data, i + j + 2 * upper_avx_word, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);
			table[2] = ttmp;
			table[6] = double3_sort < 3 ? ttmp : ttmp_times_b;
			table[10] = double3_sort < 2 ? ttmp : ttmp_times_b;
			table[14] = double3_sort < 1 ? ttmp : ttmp_times_b;
			table[18] = ttmp_times_b;
			tab20[2] = ttp;
			tab20[6] = double3_sort < 3 ? ttp : ttp_over_b;
			tab20[10] = double3_sort < 2 ? ttp : ttp_over_b;
			tab20[14] = double3_sort < 1 ? ttp : ttp_over_b;
			tab20[18] = ttp_over_b;

			gwfft_weights_fudged (gwdata->dd_data, i + j + 3 * upper_avx_word, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);
			table[3] = ttmp;
			table[7] = double4_sort < 3 ? ttmp : ttmp_times_b;
			table[11] = double4_sort < 2 ? ttmp : ttmp_times_b;
			table[15] = double4_sort < 1 ? ttmp : ttmp_times_b;
			table[19] = ttmp_times_b;
			tab20[3] = ttp;
			tab20[7] = double4_sort < 3 ? ttp : ttp_over_b;
			tab20[11] = double4_sort < 2 ? ttp : ttp_over_b;
			tab20[15] = double4_sort < 1 ? ttp : ttp_over_b;
			tab20[19] = ttp_over_b;

			table += 40;
		}
	}
	return (table);
}

/* This routine builds the big/little flags table for an AVX r4dwpn (radix-4 with partial normalization) FFT */

double *yr4dwpn_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
const	int	START_OF_CHAIN = 0x8000;
const	int	END_OF_CHAIN = 0x4000;
	int	combos[16], next_combo[16];
	unsigned char combo_recorded[256];
	unsigned char *p;
	unsigned long upper_avx_word, fftlen_over_2, num_cols;
	unsigned long group, i, j, k, m, n, num_combos;

/* Init table of first 8 big/lit values */

	memset (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES, 0, sizeof (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES));

/* Big/lit flags form a very regular pattern.  For example, if there are 18.3 b's */
/* per FFT word then you get either a big word followed by two little words or a */
/* big  followed by three little words.  Here we determine which patterns of big/lit */
/* are possible in a ynorm_wpn macro which processes 2 AVX words. */

/* Generate all possible valid combinations of big/lit flags */

	memset (combo_recorded, 0, sizeof (combo_recorded));
	upper_avx_word = gwdata->PASS2_SIZE;
	fftlen_over_2 = gwdata->FFTLEN / 2;
	num_combos = 0;

	/* Loop over all cache lines */
	for (i = 0; i < upper_avx_word; i++) {
		for (j = 0; j < fftlen_over_2; j += 4*upper_avx_word) {
			int	combo;

			/* Generate combo for this cache line */
			combo = (is_big_word (gwdata, i + j + 3*upper_avx_word) << 7) +
				(is_big_word (gwdata, i + j + 2*upper_avx_word) << 6) +
				(is_big_word (gwdata, i + j + upper_avx_word) << 5) +
				(is_big_word (gwdata, i + j) << 4) +
				(is_big_word (gwdata, i + j + fftlen_over_2 + 3*upper_avx_word) << 3) +
				(is_big_word (gwdata, i + j + fftlen_over_2 + 2*upper_avx_word) << 2) +
				(is_big_word (gwdata, i + j + fftlen_over_2 + upper_avx_word) << 1) +
				(is_big_word (gwdata, i + j + fftlen_over_2));

			/* Ignore this combo if it is a duplicate.  Otherwise, add it to our combos collection. */
			if (! combo_recorded[combo]) {
				combo_recorded[combo] = 1;
				combos[num_combos++] = combo;
			}
		}
	}

/* Concatentate combos to save space.  Let's hope they fit in 48 entries. */

	/* Init the next-in-chain array */
	for (i = 0; i < num_combos; i++)
		next_combo[i] = START_OF_CHAIN + END_OF_CHAIN;

	/* Look for 2 chains where the end of one chain has elements in common with */
	/* the start of the other chain. */

	/* Examine all chain starts */
	for (i = 0; i < num_combos; i++) {
		int	chain_end;

		/* Skip if not the start of a chain */
		if (! (next_combo[i] & START_OF_CHAIN)) continue;

		/* Find end of chain */
		for (chain_end = i; ! (next_combo[chain_end] & END_OF_CHAIN); chain_end = next_combo[chain_end] & 0xFF);

		/* Now look at all chain ends */
		for (j = 0; j < num_combos; j++) {

			/* Skip if not a chain end */
			if (! (next_combo[j] & END_OF_CHAIN)) continue;
			/* Can't chain to ourselves! */
			if (j == chain_end) continue;

			/* See if chain end has common elements with the chain start */
			if ((combos[i] >> 4) == (combos[j] & 0x0F)) {
				next_combo[j] = (next_combo[j] & START_OF_CHAIN) + i;
				next_combo[i] &= ~START_OF_CHAIN;
				break;
			}
		}
	}

/* HACK: Remember the chains in ASM_TIMERS so that we can properly build LIMIT_INVERSE */
/* and LIMIT_BIGMAX at a later time. */

	n = 0;
	for (i = 0; i < num_combos; i++) {
		if (! (next_combo[i] & START_OF_CHAIN)) continue;

		/* Output up to 4 bytes for each entry in the chain */
		for (j = i; ; j = next_combo[j] & 0xFF) {
			((char *)gwdata->ASM_TIMERS)[n++] = (combos[j] >> 4);
			if (next_combo[j] & END_OF_CHAIN) {
				((char *)gwdata->ASM_TIMERS)[n++] = combos[j] & 0xF;
				break;
			}
		}
	}
GWASSERT (n <= 24);	// Lets see what the AVX limits really are!!!
	GWASSERT (n <= 48);
	if (n > 48) gwdata->GWERROR = GWERROR_INTERNAL + 2;

/* Determine the number of column two-to-phi multipliers */

	num_cols = gwdata->PASS2_SIZE * gwdata->wpn_count;

/* Loop to build table in exactly the same order that it will be */
/* used by the assembly code. */

	p = (unsigned char *) table;
	for (group = 0; group < upper_avx_word; group += gwdata->PASS1_CACHE_LINES) {
	    for (n = 0; n < gwdata->FFTLEN / 2; n += num_cols) {
	        for (j = 0; j < num_cols; j += gwdata->PASS2_SIZE * 4) {
		    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
			for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 2) {
				unsigned long word;

/* Now set the big/little flag for a LSW in an AVX pair */
/* Otherwise, set the big/little flag for a MSW in an AVX pair */

				word = group + j + n + i + k;
				*p = is_big_word (gwdata, word);
				if (is_big_word (gwdata, word + upper_avx_word)) *p += 2;
				if (is_big_word (gwdata, word + 2 * upper_avx_word)) *p += 4;
				if (is_big_word (gwdata, word + 3 * upper_avx_word)) *p += 8;
   
/* Set the ttp and ttmp fudge flags for two pass FFTs.  The fudge flag is */
/* set if the col mult * the grp mult is b times the correct fft_weight, */
/* meaning a mul by 1/b is required to generate the correct multiplier. */
/* Since we can't do equality compares on floats, this test is a little bit */
/* cryptic. */

				if (gwfft_weight_exponent (gwdata->dd_data, word) + 0.5 <
				    gwfft_weight_exponent (gwdata->dd_data, n + k) +
				    gwfft_weight_exponent (gwdata->dd_data, group + j + i))
					*p += 16;
				if (gwfft_weight_exponent (gwdata->dd_data, word + upper_avx_word) + 0.5 <
				    gwfft_weight_exponent (gwdata->dd_data, n + k + upper_avx_word) +
				    gwfft_weight_exponent (gwdata->dd_data, group + j + i))
					*p += 32;
				if (gwfft_weight_exponent (gwdata->dd_data, word + 2 * upper_avx_word) + 0.5 <
				    gwfft_weight_exponent (gwdata->dd_data, n + k + 2 * upper_avx_word) +
				    gwfft_weight_exponent (gwdata->dd_data, group + j + i))
					*p += 64;
				if (gwfft_weight_exponent (gwdata->dd_data, word + 3 * upper_avx_word) + 0.5 <
				    gwfft_weight_exponent (gwdata->dd_data, n + k + 3 * upper_avx_word) +
				    gwfft_weight_exponent (gwdata->dd_data, group + j + i))
					*p += 128;

/* Apply our method for reducing fudge factor data from 16 combinations down to 5 possibilities. */

				if (*p & 32) *p -= 16;
				if (*p & 128) *p -= 64;
				*p = ((*p >> 6) + ((*p >> 4) & 0x3)) * 16 + (*p & 0xF);
				p++;
			}

/* Combine last two big/lit 4-bit flag values into one 6-bit flags value. */

			p -= 2;
			for (m = 0; m <= 46; m++) {
				if (((char *)gwdata->ASM_TIMERS)[m]   == (p[0] & 0xF) &&
				    ((char *)gwdata->ASM_TIMERS)[m+1] == (p[1] & 0xF))
					break;
			}
			ASSERTG (m != 47);
			if (m == 47) gwdata->GWERROR = GWERROR_INTERNAL + 3;

/* Combine 1st and 2nd fudge factor flags into one byte. */

			p[0] = ((p[0] >> 4) << 5) + ((p[1] >> 4) << 2);

/* Output the 6-bit big/lit flags value */

			p[1] = (unsigned char) (m << 2);

/* Create separate table for first 8 biglit values for carry propagation code */

			if (group + n + j + i < sizeof (asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES))
				asm_data->u.ymm.YMM_FIRST_BIGLIT_VALUES[group + n + j + i] = p[1];

/* Move pointer to next big/lit table entry */

			p += 2;
		    }
		}
	    }
	}
	return ((double *) p);
}

/* This routine builds the sin/cos table used in pass 1 by a traditional */
/* DJB radix-4 FFT  - called by gwsetup.  If this is a negacyclic FFT, */
/* then the root-of-minus-1 premultipliers are also built. */

double *r4_build_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long pass1_size, pass1_increment, first_levels_size;
	unsigned long group, i, j, k, N, temp, upper_sse2_word;
	int	pow2_count;

/* Initialize some needed constants */

	pass1_increment = gwdata->PASS2_SIZE;
	upper_sse2_word = pass1_increment / 2;
	pass1_size = gwdata->PASS1_SIZE;

/* Determine size of the first levels. */

	if (pass1_size % 7 == 0)
		first_levels_size = 28;
	else if (pass1_size % 5 == 0 && !gwdata->NEGACYCLIC_FFT)
		first_levels_size = 20;
	else
		first_levels_size = 8;

/* Count the power-of-two FFT levels after the initial FFT levels.  If odd, the */
/* the last levels will be a radix-8, if even the last levels will be a radix-4. */

	pass1_size /= first_levels_size;
	for (pow2_count = 0; (pass1_size & 1) == 0; pass1_size /= 2) pow2_count++;

/* Set pointer to table of multipliers */

	gwdata->pass1_var_data = table;

/* Loop through all the pass 1 groups in the same order the assembly code will */
/* process the groups. */

	for (group = 0; group < upper_sse2_word; group += gwdata->PASS1_CACHE_LINES) {

		pass1_size = gwdata->PASS1_SIZE;
		pass1_size /= first_levels_size; /* Complex values we're generating sin/cos data for */
		N = gwdata->PASS2_SIZE;

/* Output the sin/cos/premultiplier values for the radix-8 block that does the */
/* last 3 levels in pass 1.  NOTE:  We do not need the "j loop" (it would loop */
/* from zero to zero) when generating the sin/cos twiddle factors for the last */
/* levels of pass 1. */

		if (pow2_count & 1) {
			N = N * 8;

/* For the r4_g8cl_sixteen_reals_eight_complex_djbfft building block, output the extra */
/* sin/cos values needed for the sixteen_reals.  The sixteen_reals is done in three parts: */
/* 4 four_reals_fft, 1 eight_reals_fft, and 1 four-complex_fft.  The four_reals_fft needs */
/* four sin/cos twiddles using 2*N, the eight_reals_fft and four_complex_fft can use the same */
/* sin/cos twiddles that will be generated later for the r4r8_g8cl_eight_complex_fft8 macro. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + i);
					gwsincos1plus0123by2 (temp, pass1_increment, N*2, table);
					gwsincos1plus0123by2 (temp + upper_sse2_word, pass1_increment, N*2, table+1);
					table += 16;
				}
			}

/* Output the sin/cos values for the complex group -- specifically the r8_sg8cl_eight_complex_djbfft macro. */

			for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3,4,5,6,7 roots of N) */

				temp = group + i;
				gwsincos1plus01234567by2 (0, temp, N, table);
				gwsincos1plus01234567by2 (0, temp + upper_sse2_word, N, table+1);
				table += 32;
			}
			pass1_size /= 8;
		}

/* For the r4_four_complex_djbfft building block levels, output the sin/cos values. */

		while ((pass1_size & 3) == 0) {
			N = N * 4;

/* For the r4_eight_reals_four_complex_djbfft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N*2 / 8; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos15by2 (temp, N*2, table);
						gwsincos15by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					}
				}
			}

/* Output the sin/cos value for the complex sections, used by the r4_four_complex_djbfft macro */

			for (j = 0; j < N / 4; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				}
			}
			pass1_size /= 4;
		}

/* For the r5_five_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 5 == 0) {
			N = N * 5;

/* The r5_ten_reals building blocks require extra sin/cos */
/* values.  The ten_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 5; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos13by2 (temp, N*2, table);
						gwsincos13by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					}
				}
			}

/* Output the sin/cos data for the complex sections, (the r5_five_complex_djbfft building block). */

			for (j = 0; j < N / 5; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				}
			}
			pass1_size /= 5;
		}

/* For the r3_three_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 3 == 0) {
			N = N * 3;

/* The r3_six_reals building blocks require an extra sin/cos */
/* value.  The six_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 3; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos1by2 (temp, N*2, table);
						gwsincos1by2 (temp + upper_sse2_word, N*2, table+1);
						table += 4;
					}
				}
			}

/* Output the sin/cos data for the complex sections (used by the r3_three_complex_djbfft building block). */

			for (j = 0; j < N / 3; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos1by2 (temp, N, table);
					gwsincos1by2 (temp + upper_sse2_word, N, table+1);
					table += 4;
				}
			}
			pass1_size /= 3;
		}
		ASSERTG (pass1_size == 1);
		if (pass1_size != 1) gwdata->GWERROR = GWERROR_INTERNAL + 4;

/* Real FFTs output one last set of sin/cos values for the first 20-reals, 28-reals, or 8-reals FFT. */

		if (!gwdata->NEGACYCLIC_FFT) {
			N = gwdata->FFTLEN;
			pass1_size = gwdata->PASS1_SIZE;
			if (pass1_size % 20 == 0) {
				for (j = 0; j < N / 20; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						for (k = 1; k <= 9; k++) {	/* Create 9 twiddle factors */
							gwsincos1by2 (k * temp, N, table);
							gwsincos1by2 (k * (temp + upper_sse2_word), N, table+1);
							table += 4;
						}
					}
				}
			}
			else if (pass1_size % 28 == 0) {
				for (j = 0; j < N / 28; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						for (k = 1; k <= 13; k++) {	/* Create 13 twiddle factors */
							gwsincos1by2 (k * temp, N, table);
							gwsincos1by2 (k * (temp + upper_sse2_word), N, table+1);
							table += 4;
						}
					}
				}
			}
			else {
				for (j = 0; j < N / 8; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos125by2 (temp, N, table);
						gwsincos125by2 (temp + upper_sse2_word, N, table+1);
						table += 12;
					}
				}
			}
		}

/* For negacyclic FFTs, the first FFT level converts from real values to complex */
/* values by multiplying by a root of -1 weight and doing a butterfly.  This is */
/* simplified because weights in the bottom half are sqrt(-1) times the */
/* matching weights in the upper half.  Thus, we butterfly upper_word * weight */
/* with bottom_word * i * weight.  That equals (upper_word + i * lower_word) * weight */
/* That is just a complex multiply with the half of the butterfly output values */
/* unneeded thanks to Hermetian symmetry. */

/* The second and third levels do a standard radix-4 four-complex-FFT */
/* building block with a post-multiply by a sin/cos root of unity. */

		else {
			N = gwdata->FFTLEN / 2;
			for (j = 0; j < N / 4; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);

/* Here we compute the standard 0,1,2,3 * temp for the radix-4 sin/cos multipliers. */
/* Then we multiply in the roots-of-minus-one premultiplier.  The root-of-minus-one */
/* premultiplier was for 2N, and a root-of-minus-one-of-2N is the same as a root */
/* unity for 4N. */

					gwsincos1plus0123by2 (temp, 4 * temp, N * 4, table); /* premult + temp*0-3 */
					temp += upper_sse2_word;
					gwsincos1plus0123by2 (temp, 4 * temp, N * 4, table+1);
					table += 16;
				}
			}
		}

/* Calculate the size of each group's sin/cos/premult data for pass1_get_next_block */

		if (group == 0) gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in complex pass 2 blocks in a traditional radix-4 FFT - called by gwsetup. */

double *r4_build_pass2_complex_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, N, limit, aux_table_size;

/* We also build a smaller auxiliary table so the final several levels aren't using */
/* cache-unfriendly large strides to access sin/cos data.  If the last levels use */
/* eight_complex macros set auxiliary table size to 512, otherwise the last levels */
/* use the four_complex macros and we'll build an auxiliary table size of 256. */

	if (gwdata->PASS2_SIZE <= 640 && gwdata->PASS2_SIZE % 3 != 0)
		aux_table_size = 0;
	else {
		for (i = 0, N = gwdata->PASS2_SIZE; (N & 1) == 0; N >>= 1) i++;
		if (i < 8)
			aux_table_size = 1 << i;
		else
			aux_table_size = (i & 1) ? 512 : 256;
	}
	
/* If the pass 2 size is divisible by 3, then the first level does a */
/* radix-3 step which only requires one sin/cos value.  Alas, rather than */
/* computing the needed N/3 sin/cos values we must compute 1/2 or 2/5 times N */
/* sin/cos values for use in later r4_x4cl_2sc_four_complex_djbfft or */
/* r5_x5cl_2sc_five_complex_djbfft levels. */

	N = gwdata->PASS2_SIZE;
	if (N % 3 == 0) {
		if (!aux_table_size ||  N / aux_table_size % 2 == 0) limit = N / 2;
		else if (N % 5 == 0) limit = N * 2 / 5;
		else limit = N / 3;
		for (i = 0; i < limit; i++) {
			gwsincos1by2 (i, N, table);
			table[1] = table[0];
			table[3] = table[2];
			table += 4;
		}
	}

/* For the radix-4 and radix-5 building blocks, output two sin/cos values. */
/* If the levels above the aux_table_size are all radix-5, then we can */
/* output a slightly smaller sin/cos table. */

	else {
		if (!aux_table_size ||  N / aux_table_size % 2 == 0) limit = N / 4;
		else limit = N / 5;
		for (i = 0; i < limit; i++) {
			gwsincos12by2 (i, N, table);
			table[1] = table[0];	/* temp */
			table[3] = table[2];
			table[5] = table[4];	/* 2 * temp */
			table[7] = table[6];
			table += 8;
		}
	}

/* Build the smaller auxiliary table */

	if (aux_table_size) {
		N = aux_table_size;
		for (i = 0; i < N / 4; i++) {
			gwsincos12by2 (i, N, table);
			table[1] = table[0];	/* temp */
			table[3] = table[2];
			table[5] = table[4];	/* 2 * temp */
			table[7] = table[6];
			table += 8;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in pass 2 of real FFTs, */

double *r4_build_pass2_real_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, N;

/* Negacyclic FFTs, don't need these tables */

	if (gwdata->NEGACYCLIC_FFT) return (table);

/* Real FFTs with an initial radix-3 step needs w^n for 2N. */

	N = gwdata->PASS2_SIZE;
	if (N % 3 == 0) {
		for (i = 0; i < N / 3; i++) {
			gwsincos1by1 (i, N * 2, table);
			table += 2;
		}
		while (N % 3 == 0) N = N / 3;
	}

/* Real FFTs with a radix-5 step needs w^n and w^3n for 2N. */

	if (N % 5 == 0) {
		for (i = 0; i < N / 5; i++) {
			gwsincos13by1 (i, N * 2, table);
			table += 4;
		}
		while (N % 5 == 0) N = N / 5;
	}

/* Real FFTs with eight_reals macros need w^n and w^5n for 2N. */

	for (i = 0; i < N / 4; i++) {
		gwsincos15by1 (i, N * 2, table);
		table += 4;
	}

/* Return address of the end of the table */

	return (table);
}


/* This routine builds the fixed sin/cos premultiplier table used in pass 1 by the radix-4 */
/* delayed multiplier FFT - called by gwsetup.  It is like the traditional radix-4 FFT */
/* but with the negacyclic premultipliers split into two parts to reduce memory. */

double *r4delay_build_fixed_premult_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long pass1_increment, j, N;

/* For negacyclic FFTs, build the fixed roots-of-minus-one table. */
/* Output these roots in the same order they will be used the first two levels of pass 1. */

	if (gwdata->NEGACYCLIC_FFT) {
		N = gwdata->FFTLEN / 2;
		pass1_increment = gwdata->PASS2_SIZE;
		for (j = 0; j < N / 4; j += pass1_increment) {

/* Here we compute the roots-of-minus-one premultiplier.  The root-of-minus-one */
/* premultiplier was for 2N, and a root-of-minus-one-of-2N is the same as a root */
/* unity for 4N. */

			gwsincos1plus0123by2 (j, N / 4, N * 4, table);
			table[1] = table[0];	/* premult  */
			table[3] = table[2];
			table[5] = table[4];	/* premult * -1 ^ 1/8 */
			table[7] = table[6];
			table[9] = table[8];	/* premult * -1 ^ 2/8 */
			table[11] = table[10];
			table[13] = table[12];	/* premult * -1 ^ 3/8 */
			table[15] = table[14];
			table += 16;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in pass 1 by the radix-4/8 DJB */
/* FFT with delayed sin/cos multiplies. */

double *r4delay_build_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long pass1_size, pass1_increment, delay_count;
	unsigned long group, i, j, k, N, temp, upper_sse2_word;
	int	pow2_count;

/* Initialize some needed constants */

	pass1_increment = gwdata->PASS2_SIZE;
	upper_sse2_word = pass1_increment / 2;
	pass1_size = gwdata->PASS1_SIZE;

/* Determine number of delay groups.  In a standard radix-4 FFT, there is only one sin/cos */
/* group in the last pass 1 level.  We reduce our memory usage by using just one fixed sin/cos */
/* table in the first FFT levels and having multiple groups of sin/cos data in the last pass 1 level. */
/* I call these groups of sin/cos data in the last pass 1 level "delay groups". */

	if (pass1_size % 7 == 0)
		delay_count = 14;
	else if (pass1_size % 5 == 0 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 10;
 	else if ((pass1_size == 512 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1024 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 2560 && gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 5120 && gwdata->NEGACYCLIC_FFT) ||
		 pass1_size == 1536 || pass1_size == 2048 || pass1_size == 3072 || pass1_size == 4096)
		delay_count = 16;
	else
		delay_count = 4;

/* Count the power-of-two FFT levels after the initial FFT levels.  If odd, the */
/* the last levels will be a radix-8, if even the last levels will be a radix-4. */

	pass1_size /= (delay_count * 2);
	for (pow2_count = 0; (pass1_size & 1) == 0; pass1_size /= 2) pow2_count++;

/* Set pointer to table of multipliers */

	gwdata->pass1_var_data = table;

/* Loop through all the pass 1 groups in the same order the assembly code will */
/* process the groups. */

	for (group = 0; group < upper_sse2_word; group += gwdata->PASS1_CACHE_LINES) {

		pass1_size = gwdata->PASS1_SIZE;
		pass1_size /= (delay_count * 2);	/* Complex values we're generating sin/cos data for */
		N = gwdata->PASS2_SIZE;

/* Output the sin/cos/premultiplier values for the radix-8 block that does the */
/* last 3 levels in pass 1.  NOTE:  We do not need the "j loop" (it would loop */
/* from zero to zero) when generating the sin/cos twiddle factors for the last */
/* levels of pass 1. */

		if (pow2_count & 1) {
			N = N * 8;

/* For the r4_g8cl_sixteen_reals_eight_complex_djbfft building block, output the extra */
/* sin/cos values needed for the sixteen_reals.  The sixteen_reals is done in three parts: */
/* 4 four_reals_fft, 1 eight_reals_fft, and 1 four-complex_fft.  The four_reals_fft needs */
/* four sin/cos twiddles using 2*N, the eight_reals_fft and four_complex_fft can use the same */
/* sin/cos twiddles that will be generated later for the r4r8_g8cl_eight_complex_fft8 macro. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + i);
					gwsincos1plus0123by2 (temp, pass1_increment, N*2, table);
					gwsincos1plus0123by2 (temp + upper_sse2_word, pass1_increment, N*2, table+1);
					table += 16;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the r8_sg8cl_eight_complex_fft8 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					unsigned long bigN, ktemp, actemp;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply the part of the negacyclic premultiplier here. */

					if (gwdata->NEGACYCLIC_FFT) {
						bigN = gwdata->FFTLEN * 2;
						actemp = group + i;
					} else {
						bigN = gwdata->FFTLEN;
						actemp = 0;
					}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

					if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i) * 4;
						else if (k == 2)
							ktemp = 1 * (group + i) * 4;
						else
							ktemp = bigN - 1 * (group + i) * 4;
					} else if (gwdata->NEGACYCLIC_FFT) {
						/* 0,2,1,-1 combined with 0,2,1,-1 */
						int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i) * 4;
						else
							ktemp = bigN + kmap[k] * (group + i) * 4;
					} else if (delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i);
						else if (k == 2)
							ktemp = 1 * (group + i);
						else
							ktemp = 5 * (group + i);
					} else if (delay_count == 16) {
						/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
						int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i);
						else
							ktemp = bigN + kmap[k] * (group + i);
					} else {			/* delay_count == 10 or 14 */
						/* Multipliers for the radix-20 or radix-28 step */
						ktemp = k * (group + i);
					}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3,4,5,6,7 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

					temp = (group + i) * (bigN / N);
					gwsincos1plus01234567by2 (actemp + ktemp, temp, bigN, table); /* premult,delay and temp*0-7 */
					temp = (group + i + upper_sse2_word) * (bigN / N);
					if (gwdata->NEGACYCLIC_FFT) actemp += upper_sse2_word;
					gwsincos1plus01234567by2 (actemp + ktemp, temp, bigN, table+1);
					table += 32;
				}
			}
			pass1_size /= 8;
		}

/* Output the sin/cos/premultiplier values for the radix-4 block that does the */
/* last 2 levels in pass 1. */

		else {
			N = N * 4;

/* Output the extra sin/cos values needed for the eight_reals FFT work done */
/* on the last pass 1 level.  We double N because the real part of the FFT */
/* is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + i);
					gwsincos15by2 (temp, N*2, table);
					gwsincos15by2 (temp + upper_sse2_word, N*2, table+1);
					table += 8;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the r4_sg4cl_eight_reals_four_complex_fft4 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					unsigned long bigN, ktemp, actemp;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply the part of the negacyclic premultiplier here. */

					if (gwdata->NEGACYCLIC_FFT) {
						bigN = gwdata->FFTLEN * 2;
						actemp = group + i;
					} else {
						bigN = gwdata->FFTLEN;
						actemp = 0;
					}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

					if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i) * 4;
						else if (k == 2)
							ktemp = 1 * (group + i) * 4;
						else
							ktemp = bigN - 1 * (group + i) * 4;
					} else if (gwdata->NEGACYCLIC_FFT) {
						/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * (group + i) * 4;
							else
								ktemp = bigN + kmap[k] * (group + i) * 4;
					} else if (delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i);
						else if (k == 2)
							ktemp = 1 * (group + i);
						else
							ktemp = 5 * (group + i);
					} else if (delay_count == 16) {
						/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
						int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i);
						else
							ktemp = bigN + kmap[k] * (group + i);
					} else {			/* delay_count == 10 or 14 */
						/* Multipliers for the radix-20 or radix-28 step */
						ktemp = k * (group + i);
					}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

					temp = (group + i) * (bigN / N);
					gwsincos1plus0123by2 (actemp + ktemp, temp, bigN, table); /* premult,delay and temp*0-3 */
					temp = (group + i + upper_sse2_word) * (bigN / N);
					if (gwdata->NEGACYCLIC_FFT) actemp += upper_sse2_word;
					gwsincos1plus0123by2 (actemp + ktemp, temp, bigN, table+1);
					table += 16;
				}
			}
			pass1_size /= 4;
		}

/* For the r4_four_complex_djbfft building block levels, output the sin/cos values. */

		while ((pass1_size & 3) == 0) {
			N = N * 4;

/* For the r4_eight_reals_four_complex_djbfft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N*2 / 8; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos15by2 (temp, N*2, table);
						gwsincos15by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					}
				}
			}

/* Output the sin/cos value for the complex sections, used by the r4_four_complex_djbfft macro */

			for (j = 0; j < N / 4; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				}
			}
			pass1_size /= 4;
		}

/* For the r5_five_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 5 == 0) {
			N = N * 5;

/* The r5_ten_reals building blocks require extra sin/cos */
/* values.  The ten_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 5; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos13by2 (temp, N*2, table);
						gwsincos13by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					}
				}
			}

/* Output the sin/cos data for the complex sections, (the r5_five_complex_djbfft building block). */

			for (j = 0; j < N / 5; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				}
			}
			pass1_size /= 5;
		}

/* For the r3_three_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 3 == 0) {
			N = N * 3;

/* The r3_six_reals building blocks require an extra sin/cos */
/* value.  The six_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 3; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos1by2 (temp, N*2, table);
						gwsincos1by2 (temp + upper_sse2_word, N*2, table+1);
						table += 4;
					}
				}
			}

/* Output the sin/cos data for the complex sections (used by the r3_three_complex_djbfft building block). */

			for (j = 0; j < N / 3; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos1by2 (temp, N, table);
					gwsincos1by2 (temp + upper_sse2_word, N, table+1);
					table += 4;
				}
			}
			pass1_size /= 3;
		}
		ASSERTG (pass1_size == 1);
		if (pass1_size != 1) gwdata->GWERROR = GWERROR_INTERNAL + 5;

/* Calculate the size of each group's sin/cos/premult data for pass1_get_next_block */

		if (group == 0) gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the fixed postmultiplier table used in pass 1 of the */
/* radix-4/8 delayed DJB FFT - called by gwsetup. */

double *r4delay_build_fixed_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long pass1_increment, upper_sse2_word, pass1_size, i, j, N;

/* Initialize some needed constants */

	pass1_increment = gwdata->PASS2_SIZE;
	upper_sse2_word = pass1_increment / 2;
	pass1_size = gwdata->PASS1_SIZE;

/* Real FFTs output one shared set of sin/cos values for the first 20-reals, 28-reals, or 8-reals FFT. */

	if (!gwdata->NEGACYCLIC_FFT) {
		N = gwdata->FFTLEN;
		if (pass1_size % 20 == 0) {
			for (j = 0; j < N / 20; j += pass1_increment) {
				for (i = 1; i <= 9; i++) {	/* Create 9 twiddle factors */
					gwsincos1by2 (i * j, N, table);
					gwsincos1by2 (i * (j + upper_sse2_word), N, table+1);
					table += 4;
				}
			}
			N = N / 20;
		}
		else if (pass1_size % 28 == 0) {
			for (j = 0; j < N / 28; j += pass1_increment) {
				for (i = 1; i <= 13; i++) {	/* Create 13 twiddle factors */
					gwsincos1by2 (i * j, N, table);
					gwsincos1by2 (i * (j + upper_sse2_word), N, table+1);
					table += 4;
				}
			}
			N = N / 28;
		}
		else {
			for (j = 0; j < N / 8; j += pass1_increment) {
				gwsincos125by2 (j, N, table);
				gwsincos125by2 (j + upper_sse2_word, N, table+1);
				table += 12;
			}
			N = N / 8;
			/* Sometimes we also use a fixed sin/cos table for */
			/* the next FFT levels to further reduce memory usage. */
			if (pass1_size == 512 || pass1_size == 1024 || pass1_size == 1536 ||
			    pass1_size == 2048 || pass1_size == 3072 || pass1_size == 4096) {
				/* Output the sin/cos values for the real data */
				for (j = 0; j < N*2 / 8; j += pass1_increment) {
					gwsincos15by2 (j, N*2, table);
					gwsincos15by2 (j + upper_sse2_word, N*2, table+1);
					table += 8;
				}
				/* Output the sin/cos values for the complex data */
				for (j = 0; j < N / 4; j += pass1_increment) {
					gwsincos12by2 (j, N, table);
					gwsincos12by2 (j + upper_sse2_word, N, table+1);
					table += 8;
				}
				N = N / 4;
			}
		}
	}

/* For negacyclic FFTs, also build the fixed roots-of-minus-one table. */
/* Output these roots in the same order they will be used in the first two levels of pass 1. */

	else {
		N = gwdata->FFTLEN / 2;
		for (j = 0; j < N / 4; j += pass1_increment) {
			gwsincos12by2 (j, N, table);
			gwsincos12by2 (j + upper_sse2_word, N, table+1);
			table += 8;
		}
		N = N / 4;
		/* Sometimes we also use a fixed sin/cos table for */
		/* the next FFT levels to further reduce memory usage. */
		if (pass1_size == 1536 || pass1_size == 2048 || pass1_size == 2560 ||
		    pass1_size == 3072 || pass1_size == 4096 || pass1_size == 5120) {
			/* Output the sin/cos values for the complex data */
			for (j = 0; j < N / 4; j += pass1_increment) {
				gwsincos12by2 (j, N, table);
				gwsincos12by2 (j + upper_sse2_word, N, table+1);
				table += 8;
			}
			N = N / 4;
		}
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds the sin/cos table used in pass 1 by the radix-4/8 DJB */
/* FFT with delayed sin/cos multiplies and with partial normalization. */

double *r4dwpn_build_pass1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	struct gwasm_data *asm_data = (struct gwasm_data *) gwdata->asm_data;
	unsigned long pass1_size, pass1_increment, delay_count;
	unsigned long group, i, j, k, N, temp, upper_sse2_word;
	int	pow2_count;

/* Initialize some needed constants */

	pass1_increment = gwdata->PASS2_SIZE;
	upper_sse2_word = pass1_increment / 2;
	pass1_size = gwdata->PASS1_SIZE;

/* Determine number of delay groups.  In a standard radix-4 FFT, there is only one sin/cos */
/* group in the last pass 1 level.  We reduce our memory usage by using just one fixed sin/cos */
/* table in the first FFT levels and having multiple groups of sin/cos data in the last pass 1 level. */
/* I call these groups of sin/cos data in the last pass 1 level "delay groups". */

	if (pass1_size % 7 == 0)
		delay_count = 14;
	else if (pass1_size % 5 == 0 && !gwdata->NEGACYCLIC_FFT)
		delay_count = 10;
 	else if ((pass1_size == 512 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 1024 && !gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 2560 && gwdata->NEGACYCLIC_FFT) ||
		 (pass1_size == 5120 && gwdata->NEGACYCLIC_FFT) ||
		 pass1_size == 1536 || pass1_size == 2048 || pass1_size == 3072 || pass1_size == 4096)
		delay_count = 16;
	else
		delay_count = 4;

/* Count the power-of-two FFT levels after the initial FFT levels.  If odd, the */
/* the last levels will be a radix-8, if even the last levels will be a radix-4. */

	pass1_size /= (delay_count * 2);
	for (pow2_count = 0; (pass1_size & 1) == 0; pass1_size /= 2) pow2_count++;

/* Set count of pass 1 blocks share one set of two-to-phi grp multipliers */

	if (pow2_count & 1) gwdata->wpn_count = 8;
	else if (gwdata->PASS1_SIZE == 1792 || gwdata->PASS1_SIZE == 2048) gwdata->wpn_count = 16;
	else gwdata->wpn_count = 4;
	asm_data->count2 = gwdata->wpn_count;
	asm_data->count3 = asm_data->addcount1 / gwdata->wpn_count;

/* Set pointer to table of multipliers */

	gwdata->pass1_var_data = table;

/* Loop through all the pass 1 groups in the same order the assembly code will */
/* process the groups. */

	for (group = 0; group < upper_sse2_word; group += gwdata->PASS1_CACHE_LINES) {

		pass1_size = gwdata->PASS1_SIZE;
		pass1_size /= (delay_count * 2);	/* Complex values we're generating sin/cos data for */
		N = gwdata->PASS2_SIZE;

/* Output the sin/cos/premultiplier values for the radix-8 block that does the */
/* last 3 levels in pass 1.  NOTE:  We do not need the "j loop" (it would loop */
/* from zero to zero) when generating the sin/cos twiddle factors for the last */
/* levels of pass 1. */

		if (pow2_count & 1) {
			N = N * 8;

/* For the r4_g8cl_sixteen_reals_eight_complex_djbfft building block, output the extra */
/* sin/cos values needed for the sixteen_reals.  The sixteen_reals is done in three parts: */
/* 4 four_reals_fft, 1 eight_reals_fft, and 1 four-complex_fft.  The four_reals_fft needs */
/* four sin/cos twiddles using 2*N, the eight_reals_fft and four_complex_fft can use the same */
/* sin/cos twiddles that will be generated later for the r4r8_g8cl_eight_complex_fft8 macro. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + i);
					gwsincos1plus0123by2 (temp, pass1_increment, N*2, table);
					gwsincos1plus0123by2 (temp + upper_sse2_word, pass1_increment, N*2, table+1);
					table += 16;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the r8_sg8cl_eight_complex_fft8 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					unsigned long bigN, ktemp, actemp;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply the part of the negacyclic premultiplier here. */

					if (gwdata->NEGACYCLIC_FFT) {
						bigN = gwdata->FFTLEN * 2;
						actemp = group + i;
					} else {
						bigN = gwdata->FFTLEN;
						actemp = 0;
					}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

					if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i) * 4;
						else if (k == 2)
							ktemp = 1 * (group + i) * 4;
						else
							ktemp = bigN - 1 * (group + i) * 4;
					} else if (gwdata->NEGACYCLIC_FFT) {
						/* 0,2,1,-1 combined with 0,2,1,-1 */
						int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i) * 4;
						else
							ktemp = bigN + kmap[k] * (group + i) * 4;
					} else if (delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i);
						else if (k == 2)
							ktemp = 1 * (group + i);
						else
							ktemp = 5 * (group + i);
					} else if (delay_count == 16) {
						/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
						int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i);
						else
							ktemp = bigN + kmap[k] * (group + i);
					} else {			/* delay_count == 10 or 14 */
						/* Multipliers for the radix-20 or radix-28 step */
						ktemp = k * (group + i);
					}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3,4,5,6,7 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

					temp = (group + i) * (bigN / N);
					gwsincos1plus01234567by2 (actemp + ktemp, temp, bigN, table); /* premult,delay and temp*0-7 */
					temp = (group + i + upper_sse2_word) * (bigN / N);
					if (gwdata->NEGACYCLIC_FFT) actemp += upper_sse2_word;
					gwsincos1plus01234567by2 (actemp + ktemp, temp, bigN, table+1); /* premult,delay and temp*0-7 */
					table += 32;
				}
			}
			pass1_size /= 8;
		}

/* Output the sin/cos/premultiplier values for the radix-4 block that does the */
/* last 2 levels in pass 1. */

		else {
			N = N * 4;

/* Output the extra sin/cos values needed for the eight_reals FFT work done */
/* on the last pass 1 level.  We double N because the real part of the FFT */
/* is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + i);
					gwsincos15by2 (temp, N*2, table);
					gwsincos15by2 (temp + upper_sse2_word, N*2, table+1);
					table += 8;
				}
			}

/* Output the sin/cos values for the complex delay groups -- specifically the r4_sg4cl_eight_reals_four_complex_fft4 macro. */

			for (k = 0; k < delay_count; k++) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					unsigned long bigN, ktemp, actemp;

/* If this is a negacyclic FFT, the roots of minus 1 (same as roots of FFTLEN*2) are */
/* split to reduce memory requirements.  We apply the part of the negacyclic premultiplier here. */

					if (gwdata->NEGACYCLIC_FFT) {
						bigN = gwdata->FFTLEN * 2;
						actemp = group + i;
					} else {
						bigN = gwdata->FFTLEN;
						actemp = 0;
					}

/* Factor in the delayed part of the sin/cos multiplies from the first 2 levels.  In the first 2 levels */
/* we use a fixed sin/cos table based only on j, leaving the group+i part to be applied here by */
/* creating delay_count table entries.  For a negacyclic FFT we multiply by 0,2,1,-1*(group+i) with N = FFTLEN/2. */
/* For an all-real FFT we multiply by 0,2,1,5*(group+i) with N = FFTLEN. */

					if (gwdata->NEGACYCLIC_FFT && delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i) * 4;
						else if (k == 2)
							ktemp = 1 * (group + i) * 4;
						else
							ktemp = bigN - 1 * (group + i) * 4;
					} else if (gwdata->NEGACYCLIC_FFT) {
						/* 0,2,1,-1 combined with 0,2,1,-1 */
							int	kmap[16] = {0,8,4,-4,2,10,6,-2,1,9,5,-3,-1,7,3,-5};
							if (kmap[k] >= 0)
								ktemp = kmap[k] * (group + i) * 4;
							else
								ktemp = bigN + kmap[k] * (group + i) * 4;
					} else if (delay_count == 4) {
						if (k == 0)
							ktemp = 0;
						else if (k == 1)
							ktemp = 2 * (group + i);
						else if (k == 2)
							ktemp = 1 * (group + i);
						else
							ktemp = 5 * (group + i);
					} else if (delay_count == 16) {
						/* 0,2,1,5 combined with one 0,2,1,5 and three 0,2,1,-1 */
						int	kmap[16] = {0,8,4,20,2,18,10,-6,1,17,9,-7,5,21,13,-3};
						if (kmap[k] >= 0)
							ktemp = kmap[k] * (group + i);
						else
							ktemp = bigN + kmap[k] * (group + i);
					} else {			/* delay_count == 10 or 14 */
						/* Multipliers for the radix-20 or radix-28 step */
						ktemp = k * (group + i);
					}

/* We now calculate the standard sin/cos twiddle factors (temp*0,1,2,3 roots of N) */
/* combined with the group negacyclic premultiplier roots of minus 1 (same as roots of FFTLEN*2) */
/* combined with the delayed group multipliers. */

					temp = (group + i) * (bigN / N);
					gwsincos1plus0123by2 (actemp + ktemp, temp, bigN, table); /* premult,delay and temp*0-3 */
					temp = (group + i + upper_sse2_word) * (bigN / N);
					if (gwdata->NEGACYCLIC_FFT) actemp += upper_sse2_word;
					gwsincos1plus0123by2 (actemp + ktemp, temp, bigN, table+1);
					table += 16;
				}
			}
			pass1_size /= 4;
		}

/* Output multipliers for the four complex building blocks. */

		while ((pass1_size & 3) == 0) {

			N = N * 4;

/* For the non-wpn levels, output the sin/cos values. */

			if (N != gwdata->PASS2_SIZE * gwdata->wpn_count * 4) {

/* For the r4_eight_reals_four_complex_djbfft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

				if (!gwdata->NEGACYCLIC_FFT) {
					for (j = 0; j < N*2 / 8; j += pass1_increment) {
					    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos15by2 (temp, N*2, table);
						gwsincos15by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					    }
					}
				}

/* Output the sin/cos value for the complex sections, used by the r4_four_complex_djbfft macro */

				for (j = 0; j < N / 4; j += pass1_increment) {
				    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				    }
				}
			}

/* For the wpn building block level, output the sin/cos values. */

			else {

/* For the r4_wpn_eight_reals_four_complex_djbfft building block levels, output the extra */
/* sin/cos values needed for the eight_reals.  The eight_reals doubles N because */
/* the real part of the FFT is one level behind the complex part of the FFT. */

				if (!gwdata->NEGACYCLIC_FFT) {
					for (j = 0; j < N*2 / 8; j += pass1_increment) {
					    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos15by2_weighted (gwdata->dd_data, temp, upper_sse2_word, N*2, temp, table);
						table += 12;
					    }
					}
				}

/* Output the sin/cos value for the complex sections, used by the r4_wpn_four_complex_djbfft macro */
/* We apply the two-to-phi weight for the upper SSE2 word in the group multipliers.  There is a */
/* reason for doing it there rather than here (it reduces the number of valid fudge factor combinations */
/* for each SSE2 word from 4 to 3). */

				for (j = 0; j < N / 4; j += pass1_increment) {
				    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos012by2_weighted (gwdata->dd_data, temp, upper_sse2_word, N, temp, table);
					table += 16;
				    }
				}
			}

			pass1_size /= 4;
		}

/* For the r5_five_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 5 == 0) {
			N = N * 5;

/* The r5_ten_reals building blocks require extra sin/cos */
/* values.  The ten_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 5; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos13by2 (temp, N*2, table);
						gwsincos13by2 (temp + upper_sse2_word, N*2, table+1);
						table += 8;
					}
				}
			}

/* Output the sin/cos data for the complex sections, (the r5_five_complex_djbfft building block). */

			for (j = 0; j < N / 5; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos12by2 (temp, N, table);
					gwsincos12by2 (temp + upper_sse2_word, N, table+1);
					table += 8;
				}
			}
			pass1_size /= 5;
		}

/* For the r3_three_complex_djbfft building block levels, output the sin/cos values. */

		while (pass1_size % 3 == 0) {
			N = N * 3;

/* The r3_six_reals building blocks require an extra sin/cos */
/* value.  The six_reals doubles N because the real part of the */
/* FFT is one level behind the complex part of the FFT. */

			if (!gwdata->NEGACYCLIC_FFT) {
				for (j = 0; j < N / 3; j += pass1_increment) {
					for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
						temp = (group + j + i);
						gwsincos1by2 (temp, N*2, table);
						gwsincos1by2 (temp + upper_sse2_word, N*2, table+1);
						table += 4;
					}
				}
			}

/* Output the sin/cos data for the complex sections (used by the r3_three_complex_djbfft building block). */

			for (j = 0; j < N / 3; j += pass1_increment) {
				for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
					temp = (group + j + i);
					gwsincos1by2 (temp, N, table);
					gwsincos1by2 (temp + upper_sse2_word, N, table+1);
					table += 4;
				}
			}
			pass1_size /= 3;
		}

/* Calculate the size of each group's sin/cos/premult data for pass1_get_next_block */

		if (group == 0) gwdata->pass1_var_data_size = (unsigned long) ((char *) table - (char *) gwdata->pass1_var_data);
	}

/* Return address of the end of the table */

	return (table);
}

/* This routine builds a normalization table - used by radix-4 normalization */
/* routines.  It differs from the home-grown SSE2 routines in that the different */
/* memory layout for PFA makes this routine much simpler. */

double *r4_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table,		/* Pointer to the table to fill in */
	int	col)		/* TRUE if building column, not group, table */
{
	unsigned long i, k, num_cols;

/* Handle one-pass FFTs first, there are no group multipliers */

	if (gwdata->PASS2_SIZE == 0) {
		if (!col) return (table);

/* Loop to build table */

		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long j, table_entry;
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);
			table_entry = j >> 1;

/* Now set the entry for the MSW or LSW in an SSE2 pair */

			table[table_entry*4+(j&1)] = ttmp;
			table[table_entry*4+2+(j&1)] = ttp;
		}
		return (table + gwdata->FFTLEN + gwdata->FFTLEN);
	}

/* Two pass FFTs are handled here */

	num_cols = gwdata->PASS2_SIZE / 2;
	if (col) {

/* Loop to build table */

		for (i = 0; i < num_cols; i++) {
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Now set the entry for BOTH the MSW and LSW in an SSE2 pair */

			table[i*4] = ttmp;
			table[i*4+1] = ttmp;
			table[i*4+2] = ttp;
			table[i*4+3] = ttp;
		}
		return (table + num_cols * 4);
	}

/* Build the group multipliers table */

	else {
		unsigned long h, hlimit, haddin, m, mmult, u, umult;

/* Loop to build table */

		umult = gwdata->FFTLEN / 2;
		hlimit = gwdata->FFTLEN / 4 / (2*num_cols);
		for (h = 0; h < hlimit; h++) {
			haddin = h * 2 * num_cols;
			mmult = gwdata->FFTLEN / 4;
			for (u = 0; u < 2; u++) {
				for (m = 0; m < 2; m++) {
					for (k = 0; k < 2; k++) {
						double	ttp, ttmp;
						long	n;

/* Call double-precision routine to compute the two multipliers */

						n = haddin + u * umult + m * mmult + k * num_cols;
						gwfft_weights3 (gwdata->dd_data, n, &ttp, &ttmp, NULL);

/* Now set the entry for BOTH the MSW and LSW in an SSE2 pair */

						table[k] = ttmp;
						table[2+k] = ttp;
					}
					table += 4;
				}
			}
		}
		return (table);
	}
}

/* This routine builds a normalization table - used by radix-4 with partial */
/* normalization FFTs. */

double *r4dwpn_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, j, num_cols, upper_sse2_word, second_word_in_cache_line;

/* Build the group multipliers table */

	upper_sse2_word = gwdata->PASS2_SIZE / 2;
	second_word_in_cache_line = gwdata->FFTLEN / 4;
	num_cols = gwdata->PASS2_SIZE * gwdata->wpn_count;
	for (i = 0; i < gwdata->FFTLEN / 4; i += num_cols) {
		for (j = 0; j < gwdata->FFTLEN; j += gwdata->FFTLEN / 2) {
			double	word1_lower_weight, word1_upper_weight, word2_lower_weight, word2_upper_weight;
			int	word1_lower_sort, word1_upper_sort, word2_lower_sort, word2_upper_sort;
			double	ttp, ttmp, ttp_over_b, ttmp_times_b, *tab10;

/* The sort order of the weights determines which fudge factor combination never occur. */

			word1_lower_weight = gwfft_weight_exponent (gwdata->dd_data, i + j);
			word1_upper_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + upper_sse2_word);
			word2_lower_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + second_word_in_cache_line);
			word2_upper_weight = gwfft_weight_exponent (gwdata->dd_data, i + j + second_word_in_cache_line + upper_sse2_word);

/* Now sort the four weights */

			word1_lower_sort = (word1_lower_weight > word1_upper_weight) +
					   (word1_lower_weight > word2_lower_weight) +
					   (word1_lower_weight > word2_upper_weight);
			word1_upper_sort = (word1_upper_weight > word1_lower_weight) +
					   (word1_upper_weight > word2_lower_weight) +
					   (word1_upper_weight > word2_upper_weight);
			word2_lower_sort = (word2_lower_weight > word1_lower_weight) +
					   (word2_lower_weight > word1_upper_weight) +
					   (word2_lower_weight > word2_upper_weight);
			word2_upper_sort = (word2_upper_weight > word1_lower_weight) +
					   (word2_upper_weight > word1_upper_weight) +
					   (word2_upper_weight > word2_lower_weight);

/* Call double-precision routine to compute first set of multipliers. */
/* We compute two-to-phi multiplied by the fudge factor so that normalize won't have to. */

			gwfft_weights_fudged (gwdata->dd_data, i + j, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);

/* Set the LSW entries in an SSE2 pair */

			table[0] = ttmp;
			table[2] = word1_lower_sort < 3 ? ttmp : ttmp_times_b;
			table[4] = word1_lower_sort < 2 ? ttmp : ttmp_times_b;
			table[6] = word1_lower_sort < 1 ? ttmp : ttmp_times_b;
			table[8] = ttmp_times_b;
			tab10 = table + 10;
			tab10[0] = ttp;
			tab10[2] = word1_lower_sort < 3 ? ttp : ttp_over_b;
			tab10[4] = word1_lower_sort < 2 ? ttp : ttp_over_b;
			tab10[6] = word1_lower_sort < 1 ? ttp : ttp_over_b;
			tab10[8] = ttp_over_b;

/* Set the MSW entries in an SSE2 pair */

			gwfft_weights_fudged (gwdata->dd_data, i + j + upper_sse2_word, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);

			table[1] = ttmp;
			table[3] = word1_upper_sort < 3 ? ttmp : ttmp_times_b;
			table[5] = word1_upper_sort < 2 ? ttmp : ttmp_times_b;
			table[7] = word1_upper_sort < 1 ? ttmp : ttmp_times_b;
			table[9] = ttmp_times_b;
			tab10[1] = ttp;
			tab10[3] = word1_upper_sort < 3 ? ttp : ttp_over_b;
			tab10[5] = word1_upper_sort < 2 ? ttp : ttp_over_b;
			tab10[7] = word1_upper_sort < 1 ? ttp : ttp_over_b;
			tab10[9] = ttp_over_b;

			table += 20;

/* Repeat for the second word in the cache line */

			gwfft_weights_fudged (gwdata->dd_data, i + j + second_word_in_cache_line, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);

			table[0] = ttmp;
			table[2] = word2_lower_sort < 3 ? ttmp : ttmp_times_b;
			table[4] = word2_lower_sort < 2 ? ttmp : ttmp_times_b;
			table[6] = word2_lower_sort < 1 ? ttmp : ttmp_times_b;
			table[8] = ttmp_times_b;
			tab10 = table + 10;
			tab10[0] = ttp;
			tab10[2] = word2_lower_sort < 3 ? ttp : ttp_over_b;
			tab10[4] = word2_lower_sort < 2 ? ttp : ttp_over_b;
			tab10[6] = word2_lower_sort < 1 ? ttp : ttp_over_b;
			tab10[8] = ttp_over_b;

			gwfft_weights_fudged (gwdata->dd_data, i + j + second_word_in_cache_line + upper_sse2_word, gwdata->b, &ttp, &ttmp, &ttp_over_b, &ttmp_times_b);

			table[1] = ttmp;
			table[3] = word2_upper_sort < 3 ? ttmp : ttmp_times_b;
			table[5] = word2_upper_sort < 2 ? ttmp : ttmp_times_b;
			table[7] = word2_upper_sort < 1 ? ttmp : ttmp_times_b;
			table[9] = ttmp_times_b;
			tab10[1] = ttp;
			tab10[3] = word2_upper_sort < 3 ? ttp : ttp_over_b;
			tab10[5] = word2_upper_sort < 2 ? ttp : ttp_over_b;
			tab10[7] = word2_upper_sort < 1 ? ttp : ttp_over_b;
			tab10[9] = ttp_over_b;

			table += 20;
		}
	}
	return (table);
}

/* This routine builds a big/little flags table - used by radix-4 normalization */
/* routines.  It differs from the home-grown SSE2 routines in that the different */
/* memory layout for PFA makes this routine much simpler. */

double *r4_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned char *p;
	unsigned long h, i, j, k, m, u, gap;
	unsigned long hlimit, haddin, mmult, umult;

/* Handle one pass FFTs differently */

	if (gwdata->PASS2_SIZE == 0) {

/* Loop to build table */

		p = (unsigned char *) table;
		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long table_entry;

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);
			table_entry = j >> 1;

/* Now set the biglit table entry for a LSW in an SSE2 pair */

			if ((j & 1) == 0) {
				p[table_entry] = is_big_word (gwdata, i) * 16;
			}

/* Otherwise, set the biglit table entry for a MSW in an SSE2 pair */

			else {
				if (is_big_word (gwdata, i)) p[table_entry] += 32;
			}
		}
		return ((double *) (p + gwdata->FFTLEN / 2));
	}

/* Determine the gap between XMM high and low words */

	gap = gwdata->PASS2_SIZE / 2;

/* Loop to build table in exactly the same order that it will be */
/* used by the assembly code. */

	p = (unsigned char *) table;
	umult = gwdata->FFTLEN / 2;
	hlimit = gwdata->FFTLEN / 4 / (2*gap);
	for (i = 0; i < gap; i += gwdata->PASS1_CACHE_LINES) {
		for (h = 0; h < hlimit; h++) {
			haddin = h * 2 * gap;
			mmult = gwdata->FFTLEN / 4;
			for (j = 0; j < gwdata->PASS1_CACHE_LINES; j++) {
				for (u = 0; u < 2; u++) {
					for (m = 0; m < 2; m++) {
						for (k = 0; k < 2 * gap; k += gap) {
							unsigned long word;

/* Now set the big/little flag for a LSW in an SSE2 pair */
/* Otherwise, set the big/little flag for a MSW in an SSE2 pair */

							word = haddin + i + j + u * umult + m * mmult + k;
							if (k == 0) *p = is_big_word (gwdata, word) * 16;
							else if (is_big_word (gwdata, word)) *p += 32;

/* Set the ttp and ttmp fudge flags for two pass FFTs.  The fudge flag is */
/* set if the col mult * the grp mult is twice the correct fft_weight, */
/* meaning a mul by 0.5 is required to generate the correct multiplier. */
/* Since we can't do equality compares on floats, this test is a little bit */
/* cryptic. */

							if (gwfft_weight_exponent (gwdata->dd_data, word) + 0.5 <
							    gwfft_weight_exponent (gwdata->dd_data, word % gap) +
							    gwfft_weight_exponent (gwdata->dd_data, word - word % gap)) {
								if (k == 0) *p += 64;
								else *p += 128;
							}

/* Set some offsets that help the assembly code step through the big/lit */
/* array in a non-traditional order.  Two pass-FFTs step through the array */
/* in chunks of PASS1_CACHE_LINES, but the add, sub, and carry propagation */
/* code need to access the big/lit array linearly.  Set two global variables */
/* that tell the assembly code the big/lit array distance between words */
/* 0 and 2, and words 0 and 4. */

							if (word == 2)
								((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR2 =
									(uint32_t) ((char *) p - (char *) table);
							if (word == 4)
								((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR4 =
									(uint32_t) ((char *) p - (char *) table);
						}
						p++;
					}
				}
			}
		}
	}
	return ((double *) p);
}

/* This routine builds the big/little flags table for a r4dwpn (radix-4 with partial normalization) FFT */

double *r4dwpn_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
const	int	START_OF_CHAIN = 0x8000;
const	int	END_OF_CHAIN = 0x4000;
const	int	CHAIN_3_COMMON = 0x2000;
const	int	CHAIN_2_COMMON = 0x1000;
const	int	CHAIN_1_COMMON = 0x0800;
	int	combos[16], next_combo[16];
	unsigned char combo_recorded[256];
	unsigned char *p;
	unsigned long upper_sse2_word, fftlen_over_4, num_cols;
	unsigned long group, i, j, k, m, n, num_combos;

/* Big/lit flags form a very regular pattern.  For example, if there are 18.3 b's */
/* per FFT word then you get either a big word followed by two little words or a */
/* big  followed by three little words.  Here we determine which patterns of big/lit */
/* are possible in an xnorm_wpn macro which processes 4 SSE2 words.  There are */
/* 16 possible valid combinations which can be represented by indexing into an array */
/* of 32 SSE2 values. */

/* Generate all possible valid combinations of big/lit flags */

	memset (combo_recorded, 0, sizeof (combo_recorded));
	upper_sse2_word = gwdata->PASS2_SIZE / 2;
	fftlen_over_4 = gwdata->FFTLEN / 4;
	num_combos = 0;

	/* Loop over all cache lines */
	for (i = 0; i < upper_sse2_word; i++) {
		for (j = 0; j < fftlen_over_4; j += 2*upper_sse2_word) {
			int	combo;

			/* Generate combo for this cache line */
			combo = (is_big_word (gwdata, i + j + upper_sse2_word) << 7) +
				(is_big_word (gwdata, i + j) << 6) +
				(is_big_word (gwdata, i + j + fftlen_over_4 + upper_sse2_word) << 5) +
				(is_big_word (gwdata, i + j + fftlen_over_4) << 4) +
				(is_big_word (gwdata, i + j + 2*fftlen_over_4 + upper_sse2_word) << 3) +
				(is_big_word (gwdata, i + j + 2*fftlen_over_4) << 2) +
				(is_big_word (gwdata, i + j + 3*fftlen_over_4 + upper_sse2_word) << 1) +
				(is_big_word (gwdata, i + j + 3*fftlen_over_4));

			/* Ignore this combo if it is a duplicate.  Otherwise, add it to our combos collection. */
			if (! combo_recorded[combo]) {
				combo_recorded[combo] = 1;
				combos[num_combos++] = combo;
			}
		}
	}

/* Concatentate combos to save space.  Let's hope they fit in 48 entries. */

	/* Init the next-in-chain array */
	for (i = 0; i < num_combos; i++)
		next_combo[i] = START_OF_CHAIN + END_OF_CHAIN;

	/* Look for 2 chains where the end of one chain has elements in common with */
	/* the start of the other chain,  First look for 3 elements in common, then */
	/* two, then one. */
	for (n = 3; n != 0; n--) {

		/* Examine all chain starts */
		for (i = 0; i < num_combos; i++) {
			int	chain_end;

			/* Skip if not the start of a chain */
			if (! (next_combo[i] & START_OF_CHAIN)) continue;

			/* Find end of chain */
			for (chain_end = i; ! (next_combo[chain_end] & END_OF_CHAIN); chain_end = next_combo[chain_end] & 0xFF);

			/* Now look at all chain ends */
			for (j = 0; j < num_combos; j++) {

				/* Skip if not a chain end */
				if (! (next_combo[j] & END_OF_CHAIN)) continue;
				/* Can't chain to ourselves! */
				if (j == chain_end) continue;

				/* See if chain end has the proper number of common elements with the chain start */
				if (n == 3 && (combos[i] >> 2) == (combos[j] & 0x3F)) {
					next_combo[j] = (next_combo[j] & START_OF_CHAIN) + CHAIN_3_COMMON + i;
					next_combo[i] &= ~START_OF_CHAIN;
					break;
				}
				if (n == 2 && (combos[i] >> 4) == (combos[j] & 0x0F)) {
					next_combo[j] = (next_combo[j] & START_OF_CHAIN) + CHAIN_2_COMMON + i;
					next_combo[i] &= ~START_OF_CHAIN;
					break;
				}
				if (n == 1 && (combos[i] >> 6) == (combos[j] & 0x03)) {
					next_combo[j] = (next_combo[j] & START_OF_CHAIN) + CHAIN_1_COMMON + i;
					next_combo[i] &= ~START_OF_CHAIN;
					break;
				}
			}
		}
	}

/* HACK: Remember the chains in ASM_TIMERS so that we can properly build LIMIT_INVERSE, */
/* LIMIT_BIGMAX, and LIMIT_BIGMAX_NEG at a later time. */

	n = 0;
	for (i = 0; i < num_combos; i++) {
		if (! (next_combo[i] & START_OF_CHAIN)) continue;

		/* Output up to 4 bytes for each entry in the chain */
		for (j = i; ; j = next_combo[j] & 0xFF) {
			((char *)gwdata->ASM_TIMERS)[n++] = (combos[j] >> 6);
			if (! (next_combo[j] & CHAIN_3_COMMON))
				((char *)gwdata->ASM_TIMERS)[n++] = (combos[j] >> 4) & 3;
			if (! (next_combo[j] & (CHAIN_3_COMMON + CHAIN_2_COMMON)))
				((char *)gwdata->ASM_TIMERS)[n++] = (combos[j] >> 2) & 3;
			if (! (next_combo[j] & (CHAIN_3_COMMON + CHAIN_2_COMMON + CHAIN_1_COMMON)))
				((char *)gwdata->ASM_TIMERS)[n++] = combos[j] & 3;
			if (next_combo[j] & END_OF_CHAIN) break;
		}
	}
	GWASSERT (n <= 48);		// I've seen n get as high as 35
	if (n > 48) gwdata->GWERROR = GWERROR_INTERNAL + 6;

/* Determine the number of column two-to-phi multipliers */

	num_cols = gwdata->PASS2_SIZE * gwdata->wpn_count;

/* Loop to build table in exactly the same order that it will be */
/* used by the assembly code. */

	p = (unsigned char *) table;
	for (group = 0; group < upper_sse2_word; group += gwdata->PASS1_CACHE_LINES) {
	    for (n = 0; n < gwdata->FFTLEN / 4; n += num_cols) {
	        for (j = 0; j < num_cols; j += gwdata->PASS2_SIZE) {
		    for (i = 0; i < gwdata->PASS1_CACHE_LINES; i++) {
			for (k = 0; k < gwdata->FFTLEN; k += gwdata->FFTLEN / 4) {
			    for (m = 0; m < gwdata->PASS2_SIZE; m += upper_sse2_word) {
				unsigned long word;

/* Now set the big/little flag for a LSW in an SSE2 pair */
/* Otherwise, set the big/little flag for a MSW in an SSE2 pair */

				word = group + j + n + i + k + m;
				if (m == 0) *p = is_big_word (gwdata, word) * 16;
				else if (is_big_word (gwdata, word)) *p += 32;

/* Set the ttp and ttmp fudge flags for two pass FFTs.  The fudge flag is */
/* set if the col mult * the grp mult is b times the correct fft_weight, */
/* meaning a mul by 1/b is required to generate the correct multiplier. */
/* Since we can't do equality compares on floats, this test is a little bit */
/* cryptic. */

				if (gwfft_weight_exponent (gwdata->dd_data, word) + 0.5 <
				    gwfft_weight_exponent (gwdata->dd_data, n + k + m) +
				    gwfft_weight_exponent (gwdata->dd_data, group + j + i)) {
					if (m == 0) *p += 64;
					else *p += 128;
				}

/* Set some offsets that help the assembly code step through the big/lit */
/* array in a non-traditional order.  Two pass-FFTs step through the array */
/* in chunks of PASS1_CACHE_LINES, but the add, sub, and carry propagation */
/* code need to access the big/lit array linearly.  Set two global variables */
/* that tell the assembly code the big/lit array distance between words */
/* 0 and 2, and words 0 and 4. */

				if (word == 2)
					((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR2 =
						(uint32_t) ((char *) p - (char *) table);
				if (word == 4)
					((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR4 =
						(uint32_t) ((char *) p - (char *) table);
			    }

/* Apply our method for reducing fudge factor data by 25%.  We've observed that one */
/* of the 4 combinations never occurs. */

			    if (*p >= 128) *p -= 64;

			    p++;
			}

/* Combine last four big/lit 2-bit flag values into one 6-bit flags value. */

			p -= 4;
			for (m = 0; m <= 44; m++) {
				if (((char *)gwdata->ASM_TIMERS)[m]   == (p[0] & 48) >> 4 &&
				    ((char *)gwdata->ASM_TIMERS)[m+1] == (p[1] & 48) >> 4 &&
				    ((char *)gwdata->ASM_TIMERS)[m+2] == (p[2] & 48) >> 4 &&
				    ((char *)gwdata->ASM_TIMERS)[m+3] == (p[3] & 48) >> 4)
					break;
			}
			ASSERTG (m != 45);
			if (m == 45) gwdata->GWERROR = GWERROR_INTERNAL + 7;

/* Combine 1st and 2nd fudge factor flags.  Combine 3rd and 4th fudge factor flags. */
/* Output the 2 combined fudge factors and the 6-bit big/lit flags value using just 2 bytes. */
/* This is a more compact encoding than in previous versions. */

			p[0] = ((p[1] >> 6) + (p[0] >> 6)) * 16 + ((p[3] >> 6) + (p[2] >> 6)) * 2;
			p[1] = (unsigned char) (m << 2);
			p += 2;
		    }
		}
	    }
	}
	return ((double *) p);
}

/* This routine builds a sin/cos table - used by gwsetup */

double *hg_build_sin_cos_table (
	double	*table,		/* Pointer to the table to fill in */
	unsigned long N,	/* Number of DATA values processed by this */
				/* FFT level.  This explains the divide by 2 */
				/* for complex FFTs later in this routine */
	int	hermetian_skip,	/* True if some sin/cos values are skipped */
	int	type)		/* 0 = old style - a plain old array */
				/* 1 = SSE2 - data is duplicated */
				/* 2 = SSE2 - data is interleaved */
{
	unsigned long i;

/* Handle hermetian skip when interleaving.  First data slot is left */
/* undefined. */

	if (type == 2 && hermetian_skip) type = 3;

/* Special case the really small sin/cos tables.  If N is between 9 and 16 */
/* or between 33 and 64, then the assembly code is only doing one FFT level. */
/* In this case, the code just uses the middle sin/cos values of a 2N sized */
/* table.  We could optimize this inefficient memory usage at a later date. */

	if (N <= 8) return (table);
	if (N >= 9 && N <= 16) N = N * 2;
	if (N >= 33 && N <= 64 && type == 1 && hermetian_skip) N = N * 2;

/* In the complex case. build the same size table as the hermetian */
/* case which skips half the i values. */

	if (!hermetian_skip) N = N / 2;

/* Loop to build table. */

	for (i = hermetian_skip ? ((N & 4) ? 4 : 8) : 0; i < N; i += 4) {
		unsigned long shifted_i, shifted_N, flipped_i;
		double	sincos[6];

/* Flip the bits in i.  Our prime-factor-FFT makes this a little complex. */
/* The algorithm below works, but I've long since forgotten why. */

		shifted_i = i; shifted_N = N; flipped_i = 0;
		while ((shifted_N & 1) == 0) {
			flipped_i <<= 1;
			if (shifted_i & 1) flipped_i++;
			shifted_i >>= 1;
			shifted_N >>= 1;
		}
		flipped_i = (flipped_i * shifted_N) + shifted_i;

/* When the FFT is working on real data Hermetian symettry allows us to */
/* eliminate half of the FFT data and consequently half of the sin/cos data */
/* Case 1:  If shifted source is > shifted N/2, then we */
/* do not need these sin/cos values. */
/* Case 2:  If shifted source is zero, loop to find the top */
/* two bits.  Skip the number if the top two bits equal 3. */

		if (hermetian_skip) {
			if (shifted_i > shifted_N / 2) continue;
			if (shifted_i == 0) {
				unsigned long j;
				for (j = i; j > 3; j >>= 1);
				if (j == 3) continue;
			}
		}

/* Compute the 3 sin/cos values */

		gwsincos123by1 (flipped_i, N, (double *) &sincos);

/* Copy the sin/cos values in the appropriate way */

		if (type == 0) {
			memcpy (table, sincos, sizeof (sincos));
			table += 6;
		} else if (type == 1) {
			table[0] = table[1] = sincos[0];
			table[2] = table[3] = sincos[1];
			table[4] = table[5] = sincos[2];
			table[6] = table[7] = sincos[3];
			table[8] = table[9] = sincos[4];
			table[10] = table[11] = sincos[5];
			table += 12;
		} else if (type == 2) {
			table[0] = sincos[0];
			table[2] = sincos[1];
			table[4] = sincos[2];
			table[6] = sincos[3];
			table[8] = sincos[4];
			table[10] = sincos[5];
			type++;
		} else {
			table[1] = sincos[0];
			table[3] = sincos[1];
			table[5] = sincos[2];
			table[7] = sincos[3];
			table[9] = sincos[4];
			table[11] = sincos[5];
			type--;
			table += 12;
		}
	}
	return (table);
}

/* This routine builds a pass 2 premultiplier table - used by gwsetup */

double *hg_build_premult_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, N, incr, type, m_fudge;

/* Build a premultiplier table for the second pass incrementing by */
/* the pre-calculated pass2_size. */

	N = gwdata->FFTLEN;
	incr = gwdata->PASS2_SIZE;
	if (gwdata->NEGACYCLIC_FFT) N = N / 2;

/* Mod 2^N+1 arithmetic starts at first data set, */
/* mod 2^N-1 skips some data sets */

 	if (gwdata->NEGACYCLIC_FFT) i = 0;
	else i = incr * 4;

/* To add in the flipped_m component, we want the sin/cos of flipped_m */
/* over pass2_size.  This fudge factor will convert flipped_m into something */
/* that can be divided by N. */

	m_fudge = N / gwdata->PASS2_SIZE;

/* Loop to build table. */

	type = 0;
	for ( ; i < N; i += incr) {
		unsigned long shifted_i, shifted_N, flipped_i, k, l, m;
		unsigned long grouping_size;
		double	*table_start;
		double	sincos[2];

/* Flip the bits in i.  Our prime-factor-FFT makes this a little complex. */
/* The algorithm below works, but I've long since forgotten why. */

		shifted_i = i; shifted_N = N; flipped_i = 0;
		while ((shifted_N & 1) == 0) {
			flipped_i <<= 1;
			if (shifted_i & 1) flipped_i++;
			shifted_i >>= 1;
			shifted_N >>= 1;
		}
		flipped_i = (flipped_i * shifted_N) + shifted_i;

/* When the FFT is working on real data Hermetian symettry allows us to */
/* eliminate half of the FFT data and consequently half of the sin/cos data */
/* Case 1:  If shifted source is > shifted N/2, then we */
/* do not need these sin/cos values. */
/* Case 2:  If shifted source is zero, loop to find the top */
/* two bits.  Skip the number if the top two bits equal 3. */

		if (!gwdata->NEGACYCLIC_FFT) {
			if (shifted_i > shifted_N / 2) continue;
			if (shifted_i == 0) {
				unsigned long j;
				for (j = i; j > 3; j >>= 1);	
				if (j == 3) continue;
			}
		}

/* Generate the group multipliers.  We used to always create groups of 4, */
/* but to save memory we now group by different amounts based on pass 2 size */

		grouping_size = 4;
		if (gwdata->PASS2_SIZE == 1024) grouping_size = 8;
		if (gwdata->PASS2_SIZE == 2048) grouping_size = 8;
		if (gwdata->PASS2_SIZE == 4096) grouping_size = 16;
		if (gwdata->PASS2_SIZE == 8192) grouping_size = 16;
		table_start = table;
		for (k = 0; k < incr / 4; k += grouping_size) {

/* There are 4 multipliers in a XMM_PMD set */

			for (l = 0; l < 4; l++) {
				unsigned long real_k, pm;

/* Compute the sin/cos value (root of unity) */

				real_k = l * incr/4 + k;
				pm = real_k * flipped_i;
				if (!gwdata->NEGACYCLIC_FFT) {
					gwsincos (pm % N, N, (double *) &sincos);
				}

/* If C > 0, then also multiply by the proper root of -1.  This is done */
/* by changing the value we are taking the sin/cos of */

				else {
					pm = pm * 4 + real_k;
					gwsincos (pm % (N*4), N*4, (double *) &sincos);
				}

/* Save the premultiplier value */

				table[l*4+type] = sincos[0];
				table[l*4+2+type] = sincos[1];
			}
			table += 16;
		}
	
/* Generate the 16 column multipliers * first 4 sin/cos values. */
/* Also multiply by the LAST 4 sin/cos values so that the xsincos_complex */
/* table can be 1/4 of it's usual size.  The extra room in the cache more */
/* than compensates for the 12 extra column multipliers. */

		for (m = 0; m < 4; m++) {
		    unsigned long flipped_m;
		    flipped_m = ((m & 1) << 1) + ((m & 2) >> 1);
		    for (k = 0; k < grouping_size; k++) {
			for (l = 0; l < 4; l++) {
				unsigned long pm;

/* Compute the sin/cos value (root of unity) */

				pm = k * flipped_i +
				     l * flipped_m * N/16 +
				     (k & 3) * flipped_m * m_fudge;
				if (!gwdata->NEGACYCLIC_FFT) {
					gwsincos (pm % N, N, (double *) &sincos);
				}

/* If C > 0, then also multiply by the proper root of -1.  This is done */
/* by changing the value we are taking the sin/cos of */

				else {
					pm = pm * 4 + k;
					gwsincos (pm % (N*4), N*4, (double *) &sincos);
				}

/* Save the premultiplier value */

				table[l*4+type] = sincos[0];
				table[l*4+2+type] = sincos[1];
			}
			table += 16;
		    }
		}

		if (type == 0) table = table_start;
		type = 1 - type;
 	}

	return (table);
}

/* This routine builds a plus 1 premultiplier table - used by gwsetup */
/* when c is positive. */

double *hg_build_plus1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, j, k, l, N;
	int	pfa;

/* Set flag if this is a 3*2^n FFT */

	pfa = (gwdata->FFTLEN != pow_two_above_or_equal (gwdata->FFTLEN));

/* Adjust for two-pass FFTs */

	if (gwdata->PASS2_SIZE == 0) N = gwdata->FFTLEN;
	else N = gwdata->PASS1_SIZE * 2;

/* Loop to build premultiplier table in the same order as the underlying */
/* assembly macro needs them.  The pfa macro operates on 3 cache lines */
/* while the power-of-two macro operates on 2 cache lines. */
/* A 64 length FFT needs 0,8,16,24 for the macro then 3 more iterations */
/* for the cache lines beginning with 2,4,6. */
/* A 48 length FFT needs 0,8,16 and 4,12,20 for the first macro then */
/* one more iteration for the cache lines beginning with 2. */

	for (i = 0; i < N / (pfa ? 24 : 32); i++) {
	for (l = 0; l < 2; l++) {
		double	sincos[2];

/* Generate the pre multipliers (roots of -1). */

		for (k = 0; k < (unsigned long) (pfa ? 3 : 4); k++) {
		for (j = 0; j < 2; j++) {
			long	temp;

/* Compute the sin/cos value */

			if (pfa)
				temp = (long) ((i * 2 + l * N/12 + j + k * N/6) % N);
			else
				temp = (long) ((i * 4 + l * 2 + j + k * N/8) % N);
			gwsincos (temp, N*2, (double *) &sincos);

/* Save the premultiplier value */

			table[0+j] = sincos[0];
			table[2+j] = sincos[1];

/* For two-pass FFTs we could apply the root of -1 for the upper SSE2 */
/* double here or in the pass 2 premultipliers.  We've arbitrarily chosen */
/* to do it in the pass 2 premults. */

			if (gwdata->PASS2_SIZE) {
				j = 1;
				table[0+j] = sincos[0];
				table[2+j] = sincos[1];
			}
		}
		table += 4;
		}
	}
 	}

	return (table);
}

/* This routine builds a normalization table - used by SSE2 normalization */
/* routines */

double *hg_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table,		/* Pointer to the table to fill in */
	int	col)		/* TRUE if building column, not group, table */
{
	unsigned long i, k, num_cols;

/* Handle one-pass FFTs first, there are no group multipliers */

	if (gwdata->PASS2_SIZE == 0) {
		if (!col) return (table);

/* Loop to build table */

		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long j, table_entry;
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);
			table_entry = j >> 1;

/* Now set the entry for the MSW or LSW in an SSE2 pair */

			table[table_entry*4+(j&1)] = ttmp;
			table[table_entry*4+2+(j&1)] = ttp;
		}
		return (table + gwdata->FFTLEN + gwdata->FFTLEN);
	}

/* Two pass FFTs are handled here */

	num_cols = gwdata->PASS2_SIZE / 2;
	if (col) {

/* Loop to build table */

		for (i = 0; i < num_cols; i++) {
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Now set the entry for BOTH the MSW and LSW in an SSE2 pair */

			table[i*4] = ttmp;
			table[i*4+1] = ttmp;
			table[i*4+2] = ttp;
			table[i*4+3] = ttp;
		}
		return (table + num_cols * 4);
	}

/* Build the group multipliers table */

	else {
		unsigned long pfa, h, hlimit, haddin, m, mmult, u, umult;

/* Determine if this is a PFA 5, 6, 7, or 8 */

		for (pfa = gwdata->FFTLEN; pfa > 8; pfa >>= 1);

/* Loop to build table */

		umult = gwdata->FFTLEN / 2;
		hlimit = gwdata->FFTLEN / 4 / (2*num_cols);
		for (h = 0; h < hlimit; h++) {
			if (pfa == 5) {
				if (h < hlimit / 5) {
					haddin = h * 2 * num_cols;
					mmult = gwdata->FFTLEN / 20;
				} else {
					haddin = gwdata->FFTLEN/10 + (h - hlimit/5) * 2 * num_cols;
					mmult = gwdata->FFTLEN / 5;
				}
			} else if (pfa == 7) {
				if (h < hlimit / 7) {
					haddin = h * 2 * num_cols;
					mmult = gwdata->FFTLEN / 28;
				} else if (h < 3 * hlimit / 7) {
					haddin = gwdata->FFTLEN/14 + (h - hlimit/7) * 2 * num_cols;
					mmult = gwdata->FFTLEN / 14;
				} else {
					haddin = 3*gwdata->FFTLEN/14 + (h - 3*hlimit/7) * 2 * num_cols;
					mmult = gwdata->FFTLEN / 7;
				}
			} else {
				haddin = h * 2 * num_cols;
				mmult = gwdata->FFTLEN / 4;
			}
			for (u = 0; u < 2; u++) {
			for (m = 0; m < 2; m++) {
			for (k = 0; k < 2; k++) {
				double	ttp, ttmp;
				long	n;

/* Call double-precision routine to compute the two multipliers */

				n = haddin + u * umult + m * mmult + k * num_cols;
				gwfft_weights3 (gwdata->dd_data, n, &ttp, &ttmp, NULL);

/* Now set the entry for BOTH the MSW and LSW in an SSE2 pair */

				table[k] = ttmp;
				table[2+k] = ttp;
			}
			table += 4;
			}
			}
		}
		return (table);
	}
}

/* This routine builds a big/little flags table - used by SSE2 normalization */
/* routines */

double *hg_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned char *p;
	unsigned long h, i, j, k, m, u, gap;
	unsigned long pfa, hlimit, haddin, mmult, umult;

/* Handle one pass FFTs differently */

	if (gwdata->PASS2_SIZE == 0) {

/* Loop to build table */

		p = (unsigned char *) table;
		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long table_entry;

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);
			table_entry = j >> 1;

/* Now set the biglit table entry for a LSW in an SSE2 pair */

			if ((j & 1) == 0) {
				p[table_entry] = is_big_word (gwdata, i) * 16;
			}

/* Otherwise, set the biglit table entry for a MSW in an SSE2 pair */

			else {
				if (is_big_word (gwdata, i)) p[table_entry] += 32;
			}
		}
		return ((double *) (p + gwdata->FFTLEN / 2));
	}

/* Determine if this is a PFA 5, 6, 7, or 8 */

	for (pfa = gwdata->FFTLEN; pfa > 8; pfa >>= 1);

/* Determine the gap between XMM high and low words */

	gap = gwdata->PASS2_SIZE / 2;

/* Loop to build table in exactly the same order that it will be */
/* used by the assembly code.  This is especially ugly in the PFA cases */

	p = (unsigned char *) table;
	umult = gwdata->FFTLEN / 2;
	hlimit = gwdata->FFTLEN / 4 / (2*gap);
	for (i = 0; i < gap; i += gwdata->PASS1_CACHE_LINES) {
	for (h = 0; h < hlimit; h++) {
		if (pfa == 5) {
			if (h < hlimit / 5) {
				haddin = h * 2 * gap;
				mmult = gwdata->FFTLEN / 20;
			} else {
				haddin = gwdata->FFTLEN/10 + (h - hlimit/5) * 2 * gap;
				mmult = gwdata->FFTLEN / 5;
			}
		} else if (pfa == 7) {
			if (h < hlimit / 7) {
				haddin = h * 2 * gap;
				mmult = gwdata->FFTLEN / 28;
			} else if (h < 3 * hlimit / 7) {
				haddin = gwdata->FFTLEN/14 + (h - hlimit/7) * 2 * gap;
				mmult = gwdata->FFTLEN / 14;
			} else {
				haddin = 3*gwdata->FFTLEN/14 + (h - 3*hlimit/7) * 2 * gap;
				mmult = gwdata->FFTLEN / 7;
			}
		} else {
			haddin = h * 2 * gap;
			mmult = gwdata->FFTLEN / 4;
		}
	for (j = 0; j < gwdata->PASS1_CACHE_LINES; j++) {
	for (u = 0; u < 2; u++) {
	for (m = 0; m < 2; m++) {
	for (k = 0; k < 2 * gap; k += gap) {
		unsigned long word;

/* Now set the big/little flag for a LSW in an SSE2 pair */
/* Otherwise, set the big/little flag for a MSW in an SSE2 pair */

		word = haddin + i + j + u * umult + m * mmult + k;
		if (k == 0) *p = is_big_word (gwdata, word) * 16;
		else if (is_big_word (gwdata, word)) *p += 32;

/* Set the ttp and ttmp fudge flags for two pass FFTs.  The fudge flag is */
/* set if the col mult * the grp mult is twice the correct fft_weight, */
/* meaning a mul by 0.5 is required to generate the correct multiplier. */
/* Since we can't do equality compares on floats, this test is a little bit */
/* cryptic. */

		if (gwfft_weight_exponent (gwdata->dd_data, word) + 0.5 <
		    gwfft_weight_exponent (gwdata->dd_data, word % gap) +
		    gwfft_weight_exponent (gwdata->dd_data, word - word % gap)) {
			if (k == 0) *p += 64;
			else *p += 128;
		}

/* Set some offsets that help the assembly code step through the big/lit */
/* array in a non-traditional order.  Two pass-FFTs step through the array */
/* in chunks of PASS1_CACHE_LINES, but the add, sub, and carry propagation */
/* code need to access the big/lit array linearly.  Set two global variables */
/* that tell the assembly code the big/lit array distance between words */
/* 0 and 2, and words 0 and 4. */

		if (word == 2)
			((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR2 =
				(uint32_t) ((char *) p - (char *) table);
		if (word == 4)
			((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR4 =
				(uint32_t) ((char *) p - (char *) table);
	}
	p++;
	}
	}
	}
	}
	}
	return ((double *) p);
}

/* Ancient x87 setup routines */

#ifndef X86_64

/* This routine builds an x87 sin/cos table - used by gwsetup */

double *x87_build_sin_cos_table (
	double	*table,		/* Pointer to the table to fill in */
	unsigned long N,
	int	hermetian_skip)	/* True if some sin/cos values are skipped */
{
	unsigned long i;

/* Special case the really small sin/cos tables.  If N is between 9 and 16 */
/* then the assembly code is only doing one FFT level. */
/* In this case, the code just uses the middle sin/cos values of a 2N sized */
/* table.  We could optimize this inefficient memory usage at a later date. */

	if (N <= 8) return (table);
	if (N >= 9 && N <= 16) N = N * 2;

/* The N value passed in represents the number of real numbers that are */
/* processed in a section.  If heremetian_skip is not set, then we are */
/* instead dealing with complex numbers and there are half as many complex */
/* numbers in a section.  For example, when doing 8 levels in pass 2, this */
/* routine is called with N=512.  The first real section has 512 values, */
/* while the remaining pass 2 sections have 256 complex values. */

	if (!hermetian_skip) N = N / 2;

/* Loop to build table */

	for (i = hermetian_skip ? ((N & 4) ? 4 : 8) : 0; i < N; i += 4) {
		unsigned long shifted_i, shifted_N, flipped_i;
		double	sincos[6];

/* Flip the bits in i.  Our prime-factor-FFT makes this a little complex. */
/* The algorithm below works, but I've long since forgotten why. */

		shifted_i = i; shifted_N = N; flipped_i = 0;
		while ((shifted_N & 1) == 0) {
			flipped_i <<= 1;
			if (shifted_i & 1) flipped_i++;
			shifted_i >>= 1;
			shifted_N >>= 1;
		}
		flipped_i = (flipped_i * shifted_N) + shifted_i;

/* When the FFT is working on real data Hermetian symettry allows us to */
/* eliminate half of the FFT data and consequently half of the sin/cos data */
/* Case 1:  If shifted source is > shifted N/2, then we */
/* do not need these sin/cos values. */
/* Case 2:  If shifted source is zero, loop to find the top */
/* two bits.  Skip the number if the top two bits equal 3. */

		if (hermetian_skip) {
			if (shifted_i > shifted_N / 2) continue;
			if (shifted_i == 0) {
				unsigned long j;
				for (j = i; j > 3; j >>= 1);
				if (j == 3) continue;
			}
		}

/* Compute the 3 sin/cos values */

		gwsincos123by1 (flipped_i, N, (double *) &sincos);

/* Copy the sin/cos values to the table */

		memcpy (table, sincos, sizeof (sincos));
		table += 6;
	}
	return (table);
}

/* This routine builds a pass 2 premultiplier table - used by gwsetup */

double *x87_build_premult_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, N, incr;

/* Build a premultiplier table for the second pass incrementing by */
/* the pre-calculated pass2_size. */

	N = gwdata->FFTLEN;
	incr = gwdata->PASS2_SIZE;
	if (gwdata->NEGACYCLIC_FFT) N = N / 2;

/* Mod 2^N+1 arithmetic starts at first data set, */
/* mod 2^N-1 skips some data sets */

	if (gwdata->NEGACYCLIC_FFT) i = 0;
	else i = incr * 2;

/* Loop to build table */

	for ( ; i < N; i += incr) {
		unsigned long shifted_i, shifted_N, flipped_i, k, l;
		double	sincos[2];

/* Flip the bits in i.  Our prime-factor-FFT makes this a little complex. */
/* The algorithm below works, but I've long since forgotten why. */

		shifted_i = i; shifted_N = N; flipped_i = 0;
		while ((shifted_N & 1) == 0) {
			flipped_i <<= 1;
			if (shifted_i & 1) flipped_i++;
			shifted_i >>= 1;
			shifted_N >>= 1;
		}
		flipped_i = (flipped_i * shifted_N) + shifted_i;

/* When the FFT is working on real data Hermetian symettry allows us to */
/* eliminate half of the FFT data and consequently half of the sin/cos data */
/* Case 1:  If shifted source is > shifted N/2, then we */
/* do not need these sin/cos values. */
/* Case 2:  If shifted source is zero, loop to find the top */
/* two bits.  Skip the number if the top two bits equal 3. */

		if (!gwdata->NEGACYCLIC_FFT) {
			if (shifted_i > shifted_N / 2) continue;
			if (shifted_i == 0) {
				unsigned long j;
				for (j = i; j > 3; j >>= 1);	
				if (j == 3) continue;
			}
		}

/* Generate the group multipliers */

		for (k = 0; k < incr / 4; k += 4) {

/* There are 4 multipliers in a PMD set */

			for (l = 0; l < 4; l++) {

/* Compute the sin/cos value (root of unity) */

				if (!gwdata->NEGACYCLIC_FFT) {
					gwsincos (((l * incr/4 + k) * flipped_i) % N, N, (double *) &sincos);
				}

/* If C > 0, then also multiply by the proper root of -1.  This is done */
/* by changing the value we are taking the sin/cos of */

				else {
					gwsincos (((l * incr/4 + k) * flipped_i * 4 + l*incr/4+k) % (N*4), N*4, (double *) &sincos);
				}

/* Save the premultiplier values */

				table[l*2] = sincos[0];
				table[l*2+1] = sincos[1];
			}
			table += 8;
		}
	
/* Generate the 4 column multipliers * 4 sin/cos values */

		for (k = 0; k < 4; k++) {
			for (l = 0; l < 4; l++) {

/* Compute the sin/cos value (root of unity) */

				if (!gwdata->NEGACYCLIC_FFT) {
					gwsincos ((k * flipped_i + l * N/16) % N, N, (double *) &sincos);
				}

/* If C > 0, then also multiply by the proper root of -1.  This is done */
/* by changing the value we are taking the sin/cos of */

				else {
					gwsincos (((k * flipped_i * 2 + l * N/8) *2 + k) % (N*4), N*4, (double *) &sincos);
				}

/* Save the premultiplier value */

				table[l*2] = sincos[0];
				table[l*2+1] = sincos[1];
			}
			table += 8;
		}
 	}

	return (table);
}

/* This routine builds a plus 1 premultiplier table - used by gwsetup */
/* when c is positive. */

double *x87_build_plus1_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned long i, k, N;
	int	pfa;

/* Set flag if this is a 3*2^n FFT */

	pfa = (gwdata->FFTLEN != pow_two_above_or_equal (gwdata->FFTLEN));

/* Adjust for two-pass FFTs */

	if (gwdata->PASS2_SIZE == 0) N = gwdata->FFTLEN;
	else N = gwdata->PASS1_SIZE;

/* Loop to build premultiplier table in the same order as the underlying */
/* assembly macro needs them. */

	for (i = 0; i < N / (pfa ? 6 : 8); i++) {
		double	sincos[2];

/* Generate the pre multipliers (roots of -1) used in one three_complex */
/* or four complex macro. */

		for (k = 0; k < (unsigned long) (pfa ? 3 : 4); k++) {
			long	temp;

/* Compute the sin/cos value */

			if (pfa)
				temp = (long) ((i + k * N/6) % N);
			else
				temp = (long) ((i + k * N/8) % N);
			gwsincos (temp, N*2, (double *) &sincos);

/* Save the premultiplier value */

			table[0] = sincos[0];
			table[1] = sincos[1];
			table += 2;
		}
	}

	return (table);
}

/* This routine builds a normalization table - used by x87 normalization */
/* routines */

double *x87_build_norm_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table,		/* Pointer to the table to fill in */
	int	col)		/* TRUE if building column, not group, table */
{
	unsigned long i, k, num_cols;

/* Handle one-pass FFTs first, there are no group multipliers */

	if (gwdata->PASS2_SIZE == 0) {
		if (!col) return (table);

/* Loop to build table */

		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long j;
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);

/* Now set the appropriate table entry.  These are put into the array */
/* in the same order that the normalization code needs them. */

			table[j*2] = ttmp;
			table[j*2+1] = ttp;
		}
		return (table + gwdata->FFTLEN + gwdata->FFTLEN);
	}

/* Two pass FFTs are handled here */

	num_cols = gwdata->PASS2_SIZE;
	if (col) {

/* Loop to build columns table */

		for (i = 0; i < num_cols; i++) {
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i, &ttp, NULL, &ttmp);

/* Now set the appropriate table entry.  These are put into the array */
/* in the same order that the normalization code needs them. */

			table[i+i] = ttmp;
			table[i+i+1] = ttp;
		}
		return (table + num_cols * 2);
	}

/* Build the group multipliers table */

	else {
		unsigned long num_grps;
		
/* Loop to build group table */

		num_grps = gwdata->FFTLEN / num_cols;
		for (i = 0; i < num_grps; i++) {
			double	ttp, ttmp;

/* Call double-precision routine to compute the two multipliers */

			gwfft_weights3 (gwdata->dd_data, i * num_cols, &ttp, &ttmp, NULL);

/* Now set the appropriate table entry.  These are put into the array */
/* in the same order that the normalization code needs them. */

			if (i < num_grps / 2) k = i * 2;
			else k = (i - num_grps / 2) * 2 + 1;
			table[k+k] = ttmp;
			table[k+k+1] = ttp;
		}
		return (table + num_grps * 2);
	}
}

/* This routine builds a big/little flags table - used by x87 normalization */
/* routines */

double *x87_build_biglit_table (
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	double	*table)		/* Pointer to the table to fill in */
{
	unsigned char *p;
	unsigned long i, j, k, m;

/* Handle one pass FFTs differently */

	if (gwdata->PASS2_SIZE == 0) {

/* Loop to build table */

		p = (unsigned char *) table;
		for (i = 0; i < gwdata->FFTLEN; i++) {
			unsigned long table_entry;

/* Find where this data appears in the FFT array and in the table we are building. */

			j = addr_offset (gwdata, i) / sizeof (double);
			table_entry = j >> 1;

/* Now set the biglit table entry for a LSW in a pair */

			if ((j & 1) == 0) {
				p[table_entry] = is_big_word (gwdata, i) * 16;
			}

/* Otherwise, set the biglit table entry for a MSW in a pair */

			else {
				if (is_big_word (gwdata, i)) p[table_entry] += 32;
			}
		}
		return ((double *) (p + gwdata->FFTLEN / 2));
	}

/* Loop to build table in exactly the same order that it will be */
/* used by the assembly code. */

	p = (unsigned char *) table;
	for (i = 0; i < gwdata->PASS2_SIZE; i += gwdata->PASS1_CACHE_LINES * 2) {
	for (j = 0; j < gwdata->FFTLEN / 2; j += gwdata->PASS2_SIZE) {
	for (k = 0; k < gwdata->PASS1_CACHE_LINES * 2; k++) {
	for (m = 0; m < gwdata->FFTLEN; m += gwdata->FFTLEN / 2) {
		unsigned long word;

/* Now set the big/little flag for a LSW in a pair */
/* Otherwise, set the big/little flag for a MSW in a pair */

		word = i + j + k + m;
		if (m == 0) *p = is_big_word (gwdata, word) * 16;
		else if (is_big_word (gwdata, word)) *p += 32;

/* Set the ttp and ttmp fudge flags for two pass FFTs */
/* The fudge flag is set if col mult * grp mult will be greater than 2 */

		if (gwfft_weight_exponent (gwdata->dd_data, word) + 0.5 <
		    gwfft_weight_exponent (gwdata->dd_data, word % gwdata->PASS2_SIZE) +
		    gwfft_weight_exponent (gwdata->dd_data, word - word % gwdata->PASS2_SIZE)) {
			if (m == 0) *p += 64;
			else *p += 128;
		}

/* Set some offsets that help the assembly code step through the big/lit */
/* array in a non-traditional order.  Two pass-FFTs step through the array */
/* in chunks of PASS1_CACHE_LINES, but the add, sub, and carry propagation */
/* code need to access the big/lit array linearly.  Set two global variables */
/* that tell the assembly code the big/lit array distance between words */
/* 0 and 2, and words 0 and 4. */

		if (word == 2)
			((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR2 =
				(uint32_t) ((char *) p - (char *) table);
		if (word == 4)
			((struct gwasm_data *) gwdata->asm_data)->BIGLIT_INCR4 =
				(uint32_t) ((char *) p - (char *) table);
	}
	p++;
	}
	}
	}
	return ((double *) p);
}

/* End ancient x87 setup routines */

#endif
