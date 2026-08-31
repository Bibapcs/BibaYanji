/*----------------------------------------------------------------------
| Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
|
| This file contains routines to QA the gwnum FFT routines.
| QA can be activated by using Advanced/Time menu choice on exponent 9900.
+---------------------------------------------------------------------*/

/* TODO: test larger values of mul-by-const */
/*	all_impl should work on small ffts by using the next large fft size */
/*		to do the comparison (and/or x87) */

/* Generate a random k of the given size */

double gen_k (int bits)
{
	double	k;
	int	i;

/* To reduce number of generic reductions due to gcd (k,c) > 1, only */
/* return small k values not divisible by 3, 5, and 7 */

	do {
		k = 1.0;
		for (i = bits; --i; )
			k = k * 2.0 + ((i == 1) ? 1 : (rand () & 1));
	} while (k > 7.0 && k < 4000000000.0 &&
		 ((unsigned long) k % 3 == 0 ||
		  (unsigned long) k % 5 == 0 ||
		  (unsigned long) k % 7 == 0));
	return (k);
}

/* Generate a random c of the given size */

int gen_c (int bits)
{
	int	c, i;

/* To reduce number of generic reductions due to gcd (k,c) > 1, only */
/* return c values not divisible by 3, 5, and 7 */

	do {
		c = 1;
		for (i = bits; --i; )
			c = c * 2 + ((i == 1) ? 1 : (rand () & 1));
	} while (c > 7 && (c % 3 == 0 || c % 5 == 0 || c % 7 == 0));
	if (rand () & 1) c = -c;
	return (c);
}

/* Generate a random b */

unsigned int gen_b ()
{
	unsigned int MIN_B, MAX_B;
	unsigned int min_b_bits, max_b_bits, range_b_bits;
	unsigned int b, bbits;

/* Get control variables */

	MIN_B = IniSectionGetInt (INI_FILE, "QA", "MIN_B", 2);
	MAX_B = IniSectionGetInt (INI_FILE, "QA", "MAX_B", 2);

/* Compute number of bits to generate */
/* Note: 1/log(2) = 1.442695040888963407 */

	min_b_bits = (unsigned int)
		     floor (log ((double) MIN_B) * 1.442695040888963407);
	max_b_bits = (unsigned int)
		     ceil (log ((double) MAX_B) * 1.442695040888963407);
	range_b_bits = max_b_bits - min_b_bits + 1;

/* Loop until we successfully create an b value in the desired range */

	do {
		bbits = (unsigned int) rand () % range_b_bits + min_b_bits;
		b = 1;
		while (bbits--) b = b * 2 + (rand () & 1);
	} while (b < MIN_B || b > MAX_B);

/* Return our random b value */

	return (b);
}

/* Generate a random n */

unsigned int gen_n ()
{
	unsigned int MIN_N, MAX_N;
	unsigned int min_n_bits, max_n_bits, range_n_bits;
	unsigned int n, nbits;

/* Get control variables */

	MIN_N = IniSectionGetInt (INI_FILE, "QA", "MIN_N", 250);
	MAX_N = IniSectionGetInt (INI_FILE, "QA", "MAX_N", 70000000);

/* Compute number of bits to generate */
/* Note: 1/log(2) = 1.442695040888963407 */

	min_n_bits = (unsigned int)
			floor (log ((double) MIN_N) * 1.442695040888963407);
	max_n_bits = (unsigned int)
			ceil (log ((double) MAX_N) * 1.442695040888963407);
	range_n_bits = max_n_bits - min_n_bits + 1;

/* Loop until we successfully create an n value in the desired range */

	do {
		nbits = (unsigned int) rand () % range_n_bits + min_n_bits;
 		n = 1;
		while (nbits--) n = n * 2 + (rand () & 1);
	} while (n < MIN_N || n > MAX_N);

/* Return our random n value */

	return (n);
}


/* Compare gwnum to a giant */

static int compare_counter = 0;
static char *compare_failure_text = NULL;

void compare (int thread_num, gwhandle *gwdata, gwnum x, giant g)
{
	giant	tmp;
	int	i;

	if (gwdata->POSTFFT) return;		// We cannot compare if we've started next forward FFT
	compare_counter++;

	tmp = popg (&gwdata->gdata, ((int) gwdata->bit_length >> 5) + 13);
	gwtogiant (gwdata, x, tmp);
	if (gcompg (g, tmp) != 0) {
		gwnum	y = gwalloc (gwdata);
		char	test_desc[200], msg[300];

		if (compare_failure_text == NULL) sprintf (test_desc, "Compare #%d", compare_counter);
		else strcpy (test_desc, compare_failure_text);
		if (gw_get_maxerr (gwdata) < 0.49) sprintf (msg, "Test (%s) failed.\n", test_desc);
		else sprintf (msg, "Test (%s) failed - probably due to roundoff error\n", test_desc);
		OutputBoth (thread_num, msg);

		gianttogw (gwdata, g, y);
		if (IniSectionGetInt (INI_FILE, "QA", "DUMP_DIFF", 0)) { // Sometimes we must debug by finding which FFT words are different
			int	count, max_count;
			count = 0;
			max_count = IniSectionGetInt (INI_FILE, "QA", "DUMP_DIFF", 50);
			for (i = 0; i < (int) gwdata->FFTLEN && count < max_count; i++) {
				long val, val2;
				get_fft_value (gwdata, x, i, &val);
				get_fft_value (gwdata, y, i, &val2);
				if (val != val2) {
					char	buf[90];
					sprintf (buf, "Mismatch word: %d  was: %ld  sb: %ld\n", i, val, val2);
					OutputBoth (thread_num, buf);
					count++;
				}
			}
		}
		if (IniSectionGetInt (INI_FILE, "QA", "CORRECT_DIFF", 1)) gwcopy (gwdata, y, x);
		gwfree (gwdata, y);
	}
	if (0) {			// Test for doubles that aren't properly normalized
		long val, bigval;
		bigval = 1 << gwdata->NUM_B_PER_SMALL_WORD;
		for (i = 0; i < (int) gwdata->FFTLEN; i++) {
			get_fft_value (gwdata, x, i, &val);
			if (val < -bigval || val > bigval)
				OutputBoth (thread_num, "oflow.\n");
		}
	}
	pushg (&gwdata->gdata, 1);
}

void compare_with_text (int thread_num, gwhandle *gwdata, gwnum x, giant g, char *fail_msg)
{
	compare_failure_text = fail_msg;
	compare (thread_num, gwdata, x, g);
	compare_failure_text = NULL;
}

void compare_with_text_and_int (int thread_num, gwhandle *gwdata, gwnum x, giant g, char *fail_msg, int fail_num)
{
	char	buf[100];
	sprintf (buf, "%s #%d", fail_msg, fail_num);
	compare_with_text (thread_num, gwdata, x, g, buf);
}

/* Generate random data to start the test */

void gen_data (gwhandle *gwdata, gwnum x, giant g)
{
	unsigned long i, len;
	int	seed;
	char	buf[100];

/* Set and output seed so that we can re-generate the random data */

	seed = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_SEED", (int) time (NULL) + rand ());
	srand (seed);
	sprintf (buf, "Random seed is %d\n", seed);
	writeResults (buf);

/* Generate the random number */

	len = (((unsigned long) gwdata->bit_length) >> 5) + 1;
	for (i = 0; i < len; i++) {
		g->n[i] = ((unsigned long) rand() << 20) +
			  ((unsigned long) rand() << 10) +
			  (unsigned long) rand();
	}
//	len = 1; g->n[0] = 30000;
	g->sign = len;

// Read an LLR cert file

	int fd = _open ("foo", _O_BINARY | _O_RDONLY);
	if (fd > 0) {
		_read (fd, g->n, 16); //ignore 16 bytes
		_read (fd, &len, 4); //length in bytes
		_read (fd, g->n, len); //data
		memset ((char*) g->n + len, 0, 8);
		g->sign = divide_rounding_up (len, 4);
	}

#ifdef XXX
	ultog (gwdata->b, g);
	power (g, gwdata->n);
	dblmulg (gwdata->k, g);
	iaddg (gwdata->c, g);
	mpz_t	__N, __f;
	mpz_init (__N);
	gtompz (g, __N);
	mpz_init_set_ui (__f, 3);
//	mpz_invert (__f, __f, __N);
	mpztog (__f, g);
	mpz_clear (__N);
	mpz_clear (__f);
#endif
#ifdef YYYY
	mpz_t	__N;
	mpz_init_set_str (__N, 
			  "940298595550113209437044880150400782041500126558160757273039748840761654374277758643017009845641260178773718460021731045549833766269150463259915966750844168464904734881875009550500964490196501797797924415086495515182931",
			  10);
	mpztog (__N, g);
	mpz_clear (__N);
#endif

// Convert to gwnum

	specialmodg (gwdata, g);
	gianttogw (gwdata, g, x);
}

/* Set and print random seed */

void set_seed (
	int	thread_num)
{
	int	seed;
//	char	buf[100];

	seed = (int) time (NULL);
	srand (seed + thread_num);
//	sprintf (buf, "Random seed is %ld\n", seed);
//	OutputBoth (thread_num, buf);
}

/* Thoroughly test the current setup.  This is just like test_it except */
/* that rather than using giants code to test the results, we compare */
/* the final result to every possible FFT implementation for this FFT */
/* length.  This is useful in that more FFT code is tested, the tests are */
/* faster -- especially in the really large FFT cases where giants is not */
/* practical. */

void test_it_all (
	int	thread_num,		/* Worker number */
	struct PriorityInfo *sp_info,	/* SetPriority information */
	double	k,
	unsigned long b,
	unsigned long n,
	signed long c,
	int	threads)
{
	gwhandle gwdata;
	gwnum	x, x2, x3, x4;
	giant	g, g2, g3;
	int	i, ii, res, nth_fft, num_squarings;
	char	buf[256], fft_desc[200];

/* Init */

	g = g2 = g3 = NULL;
	num_squarings = IniSectionGetInt (INI_FILE, "QA", "NUM_SQUARINGS", 50);

/* Loop over both x87 and SSE2 implementations.  Pass 1 does x87 FFTs */
/* on SSE2 machines.  Pass 2 does the SSE2 FFTs.  Pass 3 does AVX FFTs. */

	for (ii = 1; ii <= 3; ii++) {
	    int	cpu_flags;

	    cpu_flags = CPU_FLAGS;
	    if (ii == 1) {
#ifdef X86_64
		continue;
#else
		cpu_flags &= ~(CPU_AVX | CPU_SSE2);
#endif
	    }
	    if (ii == 2) {
		if (! (cpu_flags & CPU_SSE2)) continue;
		cpu_flags &= ~CPU_AVX;
	    }
	    if (ii == 3) {
		    if (! (cpu_flags & CPU_AVX)) continue;
	    }

/* Loop over all possible FFT implementations */

	    nth_fft = 1;
	    for ( ; ; ) {
		gwinit (&gwdata);
		gwset_num_threads (&gwdata, threads);
		gwset_thread_callback (&gwdata, SetAuxThreadPriority);
		gwset_thread_callback_data (&gwdata, sp_info);
		gwdata.cpu_flags = cpu_flags;
		gwdata.qa_pick_nth_fft = nth_fft;
		res = gwsetup (&gwdata, k, b, n, c);
		nth_fft = gwdata.qa_picked_nth_fft;
		if (res) break;

/* Output a startup message */

		gwfft_description (&gwdata, fft_desc);
		sprintf (buf, "QA of %s using %s\n", gwmodulo_as_string (&gwdata), fft_desc);
		OutputBoth (thread_num, buf);

/* Alloc and init numbers */

		x = gwalloc (&gwdata);
		if (x == NULL) goto nomem;
		x2 = gwalloc (&gwdata);
		if (x2 == NULL) goto nomem;
		x3 = gwalloc (&gwdata);
		if (x3 == NULL) goto nomem;
		x4 = gwalloc (&gwdata);
		if (x4 == NULL) goto nomem;

		if (g == NULL) {
			g = allocgiant (((unsigned long) gwdata.bit_length >> 5) + 10);
			if (g == NULL) goto nomem;
			gen_data (&gwdata, x, g);
		} else
			gianttogw (&gwdata, g, x);

/* Test 50 squarings */	

		gwcopy (&gwdata, x, x2);
		gwsetnormroutine (&gwdata, 0, 1, 0); /* Enable error checking */
		for (i = 0; i < num_squarings; i++) {

			/* Test POSTFFT sometimes */
			gwstartnextfft (&gwdata, (i & 3) == 2);

			/* Test gwsetaddin without and with POSTFFT set */
			if (i == 45 || i == 46) gwsetaddin (&gwdata, -31);

			/* Test several different ways to square a number */
			if (i >= 4 && i <= 7) {
				gwfft (&gwdata, x, x);
				gwfftfftmul (&gwdata, x, x, x);
			} else if (i >= 12 && i <= 15) {
				gwfft (&gwdata, x, x3);
				gwfftmul (&gwdata, x3, x);
			} else if (i >= 20 && i <= 23) {
				gwfft (&gwdata, x, x3);
				gwcopy (&gwdata, x3, x4);
				gwfftfftmul (&gwdata, x3, x4, x);
			} else if (i >= 28 && i <= 31) {		// Test new gwmul3 option (gwfftmul with s2 != d)
				gwfft (&gwdata, x, x3);
				if (i & 1) gwmul3 (&gwdata, x, x3, x4, 0);
				else gwmul3 (&gwdata, x3, x, x4, 0);
				gwcopy (&gwdata, x4, x);
			} else
				gwsquare (&gwdata, x);

			if (i == 45 || i == 46) gwsetaddin (&gwdata, 0);
		}
		sprintf (buf, "Squares complete. MaxErr=%.10g\n", gw_get_maxerr (&gwdata));
		OutputBoth (thread_num, buf);

/* Test mul by const */

		gwsetmulbyconst (&gwdata, 3);
		gwsetnormroutine (&gwdata, 0, 1, 1);
		gwsquare (&gwdata, x);
		gwsetnormroutine (&gwdata, 0, 1, 0);

/* Test square and mul carefully */

		gwfree (&gwdata, x3); gwfree (&gwdata, x4);
		gwsetaddin (&gwdata, -42);
		gwsquare_carefully (&gwdata, x);
		gwmul_carefully (&gwdata, x, x);
		gwfree_internal_memory (&gwdata);	// Free GW_RANDOM
		gwsetaddin (&gwdata, 0);

/* Test gwaddquick, gwsubquick */

		x3 = gwalloc (&gwdata); if (x3 == NULL) goto nomem;
		x4 = gwalloc (&gwdata); if (x4 == NULL) goto nomem;
		gwadd3quick (&gwdata, x, x2, x3);
		gwsub3quick (&gwdata, x, x2, x4);

/* Test gwadd and gwsub */

		gwadd (&gwdata, x, x); gwadd (&gwdata, x, x); gwadd (&gwdata, x, x);
		gwsub (&gwdata, x3, x);
		gwadd (&gwdata, x4, x);
		gwadd3 (&gwdata, x3, x4, x2);
		gwsub3 (&gwdata, x3, x, x4);
		gwadd (&gwdata, x2, x);
		gwadd (&gwdata, x4, x);

/* Test gwaddsub */

		gwaddsub (&gwdata, x, x2);	// compute x+x2 and x-x2
		gwaddsub4 (&gwdata, x, x2, x3, x4); // compute x+x2 and x-x2
		gwadd (&gwdata, x2, x);
		gwadd (&gwdata, x3, x);
		gwadd (&gwdata, x4, x);

/* Test gwsmalladd and gwsmallmul */

		gwsmalladd (&gwdata, 0x7FFFFFFFFFFFFFFFLL, x);
		gwsmallmul (&gwdata, GWSMALLMUL_MAX-1.0, x);

/* Do some multiplies to make sure that the adds and subtracts above normalized properly. */

		gwfft (&gwdata, x, x);
		gwfftfftmul (&gwdata, x, x, x);

		gwfft (&gwdata, x, x2); gwcopy (&gwdata, x2, x); gwfftadd3 (&gwdata, x, x2, x4);
		gwfftmul (&gwdata, x4, x3);
		gwfft (&gwdata, x3, x4);
		gwfftfftmul (&gwdata, x4, x2, x);

/* Test gwmul4 routines */

		gwsquare2 (&gwdata, x, x2, 0);
		gwsquare2 (&gwdata, x2, x3, 0);
		gwaddmul4 (&gwdata, x2, x3, x, x, 0);
	
		gwsubmul4 (&gwdata, x2, x3, x, x, 0);

		gwfft (&gwdata, x, x);
		gwaddmul4 (&gwdata, x2, x3, x, x, 0);

		gwfft (&gwdata, x, x);
		gwsubmul4 (&gwdata, x2, x3, x, x, 0);

/* Print final stats */

		if (gwdata.GWERROR) OutputBoth (thread_num, "GWERROR set during calculations.\n");
		sprintf (buf, "Test complete. MaxErr=%.10g\n", gw_get_maxerr (&gwdata));
		OutputBoth (thread_num, buf);

/* Free some space (so that gwtogiant can use it for temporaries) */

		gwfree (&gwdata, x2);
		gwfree (&gwdata, x3);
		gwfree (&gwdata, x4);

/* Do the final compare */

		if (g2 == NULL) {
			g2 = allocgiant (((unsigned long) gwdata.bit_length >> 5) + 10);
			if (g2 == NULL) goto nomem;
			gwtogiant (&gwdata, x, g2);
		} else {
			g3 = allocgiant (((unsigned long) gwdata.bit_length >> 5) + 10);
			if (g3 == NULL) goto nomem;
			gwtogiant (&gwdata, x, g3);
			if (gcompg (g2, g3)) {
				strcpy (buf, "Mismatched result.\n");
				OutputBoth (thread_num, buf);
			} else {
				strcpy (buf, "Results match!\n");
				OutputBoth (thread_num, buf);
			}
			free (g3);
		}
		OutputBoth (thread_num, "\n");

/* Do next FFT implementation */

		gwdone (&gwdata);
	    }
	}
bye:	free (g);
	free (g2);
	return;

nomem:	OutputBoth (thread_num, "Out of memory\n");
	gwdone (&gwdata);
	goto bye;
}


/* Thoroughly test the current setup */

void test_it (
	int	thread_num,		/* Worker number */
	gwhandle *gwdata)
{
	gwnum	x, x2, x3, x4, x5;
	giant	g, g2, g3, g4;
	int	i, num_squarings, num_inverses, stop_reason;
	char	buf[200];
	int	SQUARE_ONLY, CHECK_OFTEN;

/* Get control variables */

	SQUARE_ONLY = IniSectionGetInt (INI_FILE, "QA", "SQUARE_ONLY", 0);
	CHECK_OFTEN = IniSectionGetInt (INI_FILE, "QA", "CHECK_OFTEN", 0);
	num_squarings = IniSectionGetInt (INI_FILE, "QA", "NUM_SQUARINGS", 50);
	num_inverses = IniSectionGetInt (INI_FILE, "QA", "NUM_INVERSES", 0);
	compare_counter = 0;

/* Alloc numbers */

	x = gwalloc (gwdata);
	x2 = gwalloc (gwdata);
	x3 = gwalloc (gwdata);
	x4 = gwalloc (gwdata);
	x5 = gwalloc (gwdata);
	g = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 4) + 20);
	g2 = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 4) + 20);
	g3 = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 4) + 20);
	g4 = popg (&gwdata->gdata, ((unsigned long) gwdata->bit_length >> 4) + 20);

/* Test single, double, and triple FFT words.  Handy when first developing an FFT. */

	if (IniSectionGetInt (INI_FILE, "QA", "SingleWordTests", 0)) {
		gwsetnormroutine (gwdata, 0, 1, 0);	/* Enable error checking */
		for (i = 0; i < (int) (gwdata->ZERO_PADDED_FFT ? gwdata->FFTLEN / 2 + 1 : gwdata->FFTLEN); i++) {
			dbltogw (gwdata, 0.0, x);
			set_fft_value (gwdata, x, i, 5000);
			gwtogiant (gwdata, x, g);
			gwsquare (gwdata, x);
			squaregi (&gwdata->gdata, g);
			specialmodg (gwdata, g);
			compare (thread_num, gwdata, x, g);
		}
		for (i = 0; i < (int) (gwdata->ZERO_PADDED_FFT ? gwdata->FFTLEN / 2 : gwdata->FFTLEN - 1); i++) {
			dbltogw (gwdata, 0.0, x);
			set_fft_value (gwdata, x, i, 5000);
			set_fft_value (gwdata, x, i+1, 3000);
			gwtogiant (gwdata, x, g);
			gwsquare (gwdata, x);
			squaregi (&gwdata->gdata, g);
			specialmodg (gwdata, g);
			compare (thread_num, gwdata, x, g);
		}
		for (i = 0; i < (int) (gwdata->ZERO_PADDED_FFT ? gwdata->FFTLEN / 2 - 1 : gwdata->FFTLEN - 2); i++) {
			dbltogw (gwdata, 0.0, x);
			set_fft_value (gwdata, x, i, 5000);
			set_fft_value (gwdata, x, i+1, 3000);
			set_fft_value (gwdata, x, i+2, 1000);
			gwtogiant (gwdata, x, g);
			gwsquare (gwdata, x);
			squaregi (&gwdata->gdata, g);
			specialmodg (gwdata, g);
			compare (thread_num, gwdata, x, g);
		}
	}

	if (IniSectionGetInt (INI_FILE, "QA", "ExtraTests", 0)) {
		gwfft (gwdata, x, x3);
		gwcopy (gwdata, x3, x);
		gwfftfftmul (gwdata, x, x, x);
		squaregi (&gwdata->gdata, g);
		specialmodg (gwdata, g);
		compare_with_text_and_int (thread_num, gwdata, x, g, "ExtraTest", 1);

		OutputBoth (thread_num, "ExtraTest2\n");
		gwcopy (gwdata, x, x3);
		gwfft (gwdata, x3, x3);
		gwfftmul (gwdata, x3, x);
		squaregi (&gwdata->gdata, g);
		specialmodg (gwdata, g);
		compare_with_text_and_int (thread_num, gwdata, x, g, "ExtraTest", 2);

		OutputBoth (thread_num, "ExtraTest3\n");
		gwfft (gwdata, x, x);
		gwcopy (gwdata, x, x3);
		gwfftfftmul (gwdata, x, x3, x);
		squaregi (&gwdata->gdata, g);
		specialmodg (gwdata, g);
		compare_with_text_and_int (thread_num, gwdata, x, g, "ExtraTest", 3);

		OutputBoth (thread_num, "ExtraTest4\n");
		gwfft (gwdata, x, x);
		gwcopy (gwdata, x, x3);
		gwfftfftmul (gwdata, x3, x, x);
		squaregi (&gwdata->gdata, g);
		specialmodg (gwdata, g);
		compare_with_text_and_int (thread_num, gwdata, x, g, "ExtraTest", 4);
	}

/* Init with random starting QA values */

	gen_data (gwdata, x, g);
	if (CHECK_OFTEN) compare (thread_num, gwdata, x, g);
	gwcopy (gwdata, x, x2); gtog (g, g2);

/* Test 50 squarings */	

	gwsetnormroutine (gwdata, 0, 1, 0);	/* Enable error checking */
	for (i = 0; i < num_squarings; i++) {

		/* Test POSTFFT sometimes */
		gwstartnextfft (gwdata, (i & 3) == 2);

		/* Test gwsetaddin without and with POSTFFT set */
		if (i == 45 || i == 46) gwsetaddin (gwdata, -31);

		/* Test several different ways to square a number */
		if (i % 50 == 25 || i % 50 == 26) {			// Deprecated safemul on Montgomery mod numbers had problems -- reported by Atnashev
			gwfft (gwdata, x, x3);
			gwsafemul (gwdata, x, x3);
			if (i % 50 == 25) gwcopy (gwdata, x3, x);
			else gwsquare (gwdata, x);
		} else if (i % 50 >= 4 && i % 50 <= 7) {
			gwfft (gwdata, x, x);
			gwfftfftmul (gwdata, x, x, x);
		} else if (i % 50 >= 12 && i % 50 <= 15) {
			gwfft (gwdata, x, x3);
			gwfftmul (gwdata, x3, x);
		} else if (i % 50 >= 20 && i % 50 <= 23) {
			gwfft (gwdata, x, x3);
			gwcopy (gwdata, x3, x4);
			gwfftfftmul (gwdata, x3, x4, x);
		} else if (i % 50 >= 28 && i % 50 <= 31) {		// Test new gwmul3 option (gwfftmul with s2 != d)
			gwfft (gwdata, x, x3);
			if (i & 1) gwmul3 (gwdata, x, x3, x4, 0);
			else gwmul3 (gwdata, x3, x, x4, 0);
			gwcopy (gwdata, x4, x);
		} else if (i % 50 == 34) {				// Test postfft with unfft (zero padding had issues in past)
			gwmul3 (gwdata, x, x, x4, GWMUL_STARTNEXTFFT);
			gwunfft (gwdata, x4, x);
		} else
			gwsquare (gwdata, x);

		/* Square number (and do add-in) using giants code */
		squaregi (&gwdata->gdata, g);
		if (i == 45 || i == 46) {
			iaddg (-31, g);
			gwsetaddin (gwdata, 0);
		}
		specialmodg (gwdata, g);

		/* Compare results */
		if (CHECK_OFTEN) compare_with_text_and_int (thread_num, gwdata, x, g, "Square", i);
	}
	if (SQUARE_ONLY) goto done;

	/* Report interim results */
	sprintf (buf, "Squares complete. MaxErr=%.10g\n", gw_get_maxerr (gwdata));
	OutputBoth (thread_num, buf);

/* Test giants' modular inverse code */

	if (num_inverses) {
		if (gwdata->GENERAL_MOD) gtog (gwdata->GW_MODULUS, g3);
		else {
			ultog (gwdata->b, g3);
			power (g3, gwdata->n);
			dblmulg (gwdata->k, g3);
			iaddg (gwdata->c, g3);
		}
		for (i = 0; i < num_inverses; i++) {
			gtog (g, g4);
			g4->n[g4->sign-2] += i;
			stop_reason = invgi (&gwdata->gdata, thread_num, g3, g4);
			if (stop_reason) goto done;
			if (g4->sign < 0) {  /* Factor - verify it */
				uint32_t j;
				j = g3->n[0];
				negg (g4);
				divgi (&gwdata->gdata, g4, g3);
				stop_reason = mulgi (&gwdata->gdata, g4, g3);
				if (stop_reason) goto done;
				if (j != g3->n[0]) OutputBoth (thread_num, "Invgi factor validation failed.\n");
			} else {	/* No factor - verify inverse */
				stop_reason = invgi (&gwdata->gdata, thread_num, g3, g4);
				if (stop_reason) goto done;
				g4->n[g4->sign-2] -= i;
				if (gcompg (g, g4)) OutputBoth (thread_num, "Invgi inverse validation failed.\n");
			}
		}
	}

/* Test mul by const */

	gwsetmulbyconst (gwdata, 3);
	gwsetnormroutine (gwdata, 0, 1, 1);
	gwsquare (gwdata, x);
	gwsetnormroutine (gwdata, 0, 1, 0);
	squaregi (&gwdata->gdata, g); imulg (3, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mul by const 3");

	gwsetmulbyconst (gwdata, -3);
	gwsetnormroutine (gwdata, 0, 1, 1);
	gwsquare (gwdata, x);
	gwsetnormroutine (gwdata, 0, 1, 0);
	squaregi (&gwdata->gdata, g); imulg (-3, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mul by const -3");

/* Test without error checking as that runs different normalization code */

	gwsetnormroutine (gwdata, 0, 0, 0);
	gwsquare (gwdata, x);
	gwsetnormroutine (gwdata, 0, 1, 0);
	squaregi (&gwdata->gdata, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "no echk");

	gwsetmulbyconst (gwdata, 5);
	gwsetnormroutine (gwdata, 0, 0, 1);
	gwsquare (gwdata, x);
	gwsetnormroutine (gwdata, 0, 1, 0);
	squaregi (&gwdata->gdata, g); imulg (5, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "const, no echk");

/* Test square and mul carefully */

	gwfree (gwdata, x3); gwfree (gwdata, x4);
	gwsetaddin (gwdata, -42);
	gwsquare_carefully (gwdata, x);
	squaregi (&gwdata->gdata, g); iaddg (-42, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "square careful");

	x3 = gwalloc (gwdata);
	gwcopy (gwdata, x, x3);
	gwmul_carefully (gwdata, x3, x);
	squaregi (&gwdata->gdata, g); iaddg (-42, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mul careful");
	gwsetaddin (gwdata, 0);

	gwfft (gwdata, x, x);
	gwsquare_carefully (gwdata, x);
	squaregi (&gwdata->gdata, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "square fft careful");

	gwcopy (gwdata, x, x3);
	gwfft (gwdata, x, x);
	gwfft (gwdata, x3, x3);
	gwmul_carefully (gwdata, x3, x);
	squaregi (&gwdata->gdata, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mul fft careful");

	gwsetmulbyconst (gwdata, 3);
	gwsetnormroutine (gwdata, 0, 1, 1);
	gwsquare_carefully (gwdata, x);
	squaregi (&gwdata->gdata, g); imulg (3, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "square mul-const careful");

	gwmul_carefully (gwdata, x, x);
	gwfree_internal_memory (gwdata);	// Free GW_RANDOM
	squaregi (&gwdata->gdata, g); imulg (3, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mul mul-const careful");
	gwsetnormroutine (gwdata, 0, 1, 0);

/* Test gwunfft */

	gwfft (gwdata, x, x);
	gwunfft (gwdata, x, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "unfft1");
	gwmul3 (gwdata, x, x, x, GWMUL_STARTNEXTFFT);
	gwunfft (gwdata, x, x);
	squaregi (&gwdata->gdata, g);
	specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "unfft2");
	gwfft_for_fma (gwdata, x, x);
	gwunfft (gwdata, x, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "unfft3");

/* Test gwaddquick, gwsubquick */

	x4 = gwalloc (gwdata);
	gwadd3quick (gwdata, x, x2, x3); gtog (g, g3); addg (g2, g3);
	gwsub3quick (gwdata, x, x2, x4); gtog (g, g4); subg (g2, g4);
	if (CHECK_OFTEN) {
		specialmodg (gwdata, g3); compare_with_text (thread_num, gwdata, x3, g3, "add quick");
		specialmodg (gwdata, g4); compare_with_text (thread_num, gwdata, x4, g4, "sub quick");
	}

/* Test gwadd and gwsub */

	gwadd (gwdata, x, x); gwadd (gwdata, x, x); gwadd (gwdata, x, x);
	imulg (8, g);
	gwsub (gwdata, x3, x); subg (g3, g);
	gwadd (gwdata, x4, x); addg (g4, g);
	gwadd3 (gwdata, x3, x4, x2); gtog (g3, g2); addg (g4, g2);
	gwsub3 (gwdata, x3, x, x4); gtog (g3, g4); subg (g, g4);
	if (CHECK_OFTEN) {
		specialmodg (gwdata, g); compare_with_text (thread_num, gwdata, x, g, "add1");
		specialmodg (gwdata, g2); compare_with_text (thread_num, gwdata, x2, g2, "add2");
		specialmodg (gwdata, g4); compare_with_text (thread_num, gwdata, x4, g4, "add3");
	}
	gwadd (gwdata, x2, x); addg (g2, g);
	gwadd (gwdata, x4, x); addg (g4, g);

/* Test gwaddsub */

	gwaddsub (gwdata, x, x2);	// compute x+x2 and x-x2
	subg (g, g2);		// x2-x
	addg (g, g);		// x+x
	addg (g2, g);		// x+x2
	negg (g2);		// x-x2
	gwaddsub4 (gwdata, x, x2, x3, x4);	// compute x+x2 and x-x2
	gtog (g, g3); addg (g2, g3);
	gtog (g, g4); subg (g2, g4);
	if (CHECK_OFTEN) {
		specialmodg (gwdata, g); compare_with_text (thread_num, gwdata, x, g, "addsub1");
		specialmodg (gwdata, g2); compare_with_text (thread_num, gwdata, x2, g2, "addsub2");
		specialmodg (gwdata, g3); compare_with_text (thread_num, gwdata, x3, g3, "addsub3");
		specialmodg (gwdata, g4); compare_with_text (thread_num, gwdata, x4, g4, "addsub4");
	}
	gwadd (gwdata, x2, x); addg (g2, g);
	gwadd (gwdata, x3, x); addg (g3, g);
	gwadd (gwdata, x4, x); addg (g4, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "addsub5");

/* Test gwsmalladd and gwsmallmul */

	gwsmalladd (gwdata, 0x7FFFFFFFFFFFFFFFLL, x);
	ulltog (0x7FFFFFFFFFFFFFFFULL, g4); addg (g4, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smalladd");
	i = rand() % 10 + 2;
	gwsmallmul (gwdata, i, x);
	ulmulg (i, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smallmul1");
	gwsmallmul (gwdata, GWSMALLMUL_MAX-1.0, x);
	ulmulg ((unsigned long) (GWSMALLMUL_MAX-1.0), g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smallmul2");

/* Do some multiplies to make sure that the adds and subtracts above normalized properly. */

	gwfft (gwdata, x, x);
	gwfftfftmul (gwdata, x, x, x);
	squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smallmul3");

	gwmul3 (gwdata, x, x2, x, 0); mulgi (&gwdata->gdata, g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smallmul4");

	gwmul3 (gwdata, x, x3, x, 0); mulgi (&gwdata->gdata, g3, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "smallmul5");

/* Test partial fft mul with s2 == d */
	
	gwsquare2 (gwdata, x, x, GWMUL_STARTNEXTFFT); squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	gwfft (gwdata, x2, x2);
	gwmul3 (gwdata, x, x2, x2, 0); mulgi (&gwdata->gdata, g, g2); specialmodg (gwdata, g2);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "partial fft mul #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x2, g2, "partial fft mul #2");

/* Test gwmul4 routines */

	gwsquare2 (gwdata, x, x2, GWMUL_FFT_S1); gtog (g, g2); squaregi (&gwdata->gdata, g2); specialmodg (gwdata, g2);
	gwsquare2 (gwdata, x2, x3, GWMUL_FFT_S1); gtog (g2, g3); squaregi (&gwdata->gdata, g3); specialmodg (gwdata, g3);
	gwaddmul4 (gwdata, x2, x, x3, x4, GWMUL_PRESERVE_S3); gtog (g2, g4); addg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul nosave");

	gwaddmul4 (gwdata, x2, x, x3, x4, GWMUL_FFT_S3); //gtog (g2, g4); addg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul save");

	gwunfft (gwdata, x, x);
	gwaddmul4 (gwdata, x2, x, x3, x4, GWMUL_PRESERVE_S2); //gtog (g2, g4); addg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul132 nosave");

	gwunfft (gwdata, x, x);
	gwaddmul4 (gwdata, x2, x, x3, x4, GWMUL_FFT_S2); //gtog (g2, g4); addg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul132 save");

	gwunfft (gwdata, x, x);
	gwaddmul4 (gwdata, x2, x, x, x4, GWMUL_PRESERVE_S2 | GWMUL_PRESERVE_S3); gtog (g2, g4); addg (g, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul133 nosave");

	gwunfft (gwdata, x, x);
	gwaddmul4 (gwdata, x2, x, x, x4, GWMUL_FFT_S2 | GWMUL_FFT_S3); //gtog (g2, g4); addg (g, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul133 save");

	gwunfft (gwdata, x3, x3);
	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_PRESERVE_S3); gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul nosave");

	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_FFT_S3); //gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul save");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_PRESERVE_S2); //gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul132 nosave");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_FFT_S2); //gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul132 save");

	gwunfft (gwdata, x2, x2);
	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_PRESERVE_S1); //gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul312 nosave");

	gwunfft (gwdata, x2, x2);
	gwsubmul4 (gwdata, x2, x, x3, x4, GWMUL_FFT_S1); //gtog (g2, g4); subg (g, g4); mulg (g3, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul312 save");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x, x, x4, GWMUL_PRESERVE_S2 | GWMUL_PRESERVE_S3); gtog (g2, g4); subg (g, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul133 nosave");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x, x, x4, GWMUL_FFT_S2 | GWMUL_FFT_S3); //gtog (g2, g4); subg (g, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul133 save");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x, x2, x, x4, GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S3); gtog (g, g4); subg (g2, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul133 nosave");

	gwunfft (gwdata, x, x);
	gwsubmul4 (gwdata, x, x2, x, x4, GWMUL_FFT_S1 | GWMUL_FFT_S3); //gtog (g2, g4); subg (g, g4); mulg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul133 save");

	gwunfft (gwdata, x2, x2);
	gwsubmul4 (gwdata, x2, x3, x, x3, GWMUL_FFT_S1); gtog (g2, g4); subg (g3, g4); mulg (g, g4); gtog (g4, g3); specialmodg (gwdata, g3);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x3, g3, "submul312 save s2 == d");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x2, g2, "submul312 save s2 == d arg1");

	// Older tests
	gwcopy (gwdata, x2, x4);
	gwsubmul4 (gwdata, x4, x3, x, x4, 0); gtog (g2, g4); subg (g3, g4); mulg (g4, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "submul4 s1 == d");

	gwfft (gwdata, x, x);
	gwaddmul4 (gwdata, x2, x3, x, x, 0); gtog (g2, g4); addg (g3, g4); mulg (g4, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "addmul4fft");

	gwcopy (gwdata, x3, x4);
	gwfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x4, x, x4, 0); gtog (g2, g4); subg (g3, g4); mulg (g4, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "submul4fft");

	gwcopy (gwdata, x3, x4);
	gwfft (gwdata, x, x);
	gwsubmul4 (gwdata, x2, x4, x, x4, 0); gtog (g2, g4); subg (g3, g4); mulg (g4, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "submul4fft");

	gwmuladd4 (gwdata, x, x3, x2, x, 0); mulg (g3, g); addg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 s1 != s2");

	gwmulsub4 (gwdata, x3, x, x2, x, 0); mulg (g3, g); subg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulsub4 s1 != s2");

	gwfft (gwdata, x, x);
	gwfft (gwdata, x3, x3);
	gwmuladd4 (gwdata, x, x3, x2, x, 0); mulg (g3, g); addg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4fft s1 != s2");

	gwfft (gwdata, x, x);
	gwfft (gwdata, x3, x3);
	gwmulsub4 (gwdata, x3, x, x2, x, 0); mulg (g3, g); subg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulsub4fft s1 != s2");

	gwmuladd4 (gwdata, x, x, x2, x, 0); squaregi (&gwdata->gdata, g); addg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 s1 == s2");

	gwsetaddin (gwdata, 2);
	gwmuladd4 (gwdata, x, x, x2, x4, GWMUL_ADDINCONST); squaregi (&gwdata->gdata, g); addg (g2, g); iaddg (2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 s1 == s2");
	gwsetaddin (gwdata, 0);

	gwmulsub4 (gwdata, x, x, x2, x, 0); squaregi (&gwdata->gdata, g); subg (g2, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulsub4 s1 == s2");

	gwmulsub4 (gwdata, x, x, x2, x4, 0); squaregi (&gwdata->gdata, g); subg (g2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulsub4 s1 == s2");

	gwcopy (gwdata, x2, x4);
	gwmuladd4 (gwdata, x, x3, x4, x4, 0); mulg (g3, g); addg (g2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 s1 != s2, s3 == d");

	gwcopy (gwdata, x2, x4);
	gwmuladd4 (gwdata, x, x, x4, x4, 0); squaregi (&gwdata->gdata, g); addg (g2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 s1 == s2, s3 == d");

	gwcopy (gwdata, x2, x4); gtog (g2, g4);
	gwmulmuladd5 (gwdata, x, x, x3, x4, x4, 0); squaregi (&gwdata->gdata, g); mulg (g3, g4); addg (g, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mulmuladd5 s1 == s2, s4 == d");

	gwcopy (gwdata, x3, x); gtog (g3, g);
	gwmulmulsub5 (gwdata, x, x2, x3, x4, x4, 0); mulg (g2, g); mulg (g3, g4); subg (g4, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulmulsub5 s1 != s2, s4 == d");

	gwsetaddin (gwdata, 2);
	gwfft_for_fma (gwdata, x2, x4);
	gwmuladd4 (gwdata, x, x3, x4, x4, GWMUL_ADDINCONST); mulg (g3, g); addg (g2, g); iaddg (2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 fft-for-fma, s1 != s2");
	gwsetaddin (gwdata, 0);

	gwfft_for_fma (gwdata, x2, x4);
	gwmuladd4 (gwdata, x, x, x4, x4, 0); squaregi (&gwdata->gdata, g); addg (g2, g); specialmodg (gwdata, g);
	gwcopy (gwdata, x4, x);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 fft-for-fma, s1 == s2");

/* Test gwmul3 request to fft both sources */

	gwcopy (gwdata, x, x2);
	gwcopy (gwdata, x, x3);
	gwmul3 (gwdata, x2, x3, x, GWMUL_FFT_S1 | GWMUL_FFT_S2);
	squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "gwmul3 two fft");
	if (x2[0] != x3[0]) OutputBoth (thread_num, "FAIL: gwmul3 two fft -- s1 != s2");

/* Test gwmul3, gwmul4 where there are 2 destinations */

	gwfft (gwdata, x, x2);
	ASSERTG (FFT_state(x) != FULLY_FFTed);
	gwsquare2 (gwdata, x, x3, GWMUL_FFT_S1);
	ASSERTG (FFT_state(x) == FULLY_FFTed);
	gwsquare2 (gwdata, x, x, 0);
	squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "gwsquare2 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x3, g, "gwsquare2 two dest #2");

	gwfft (gwdata, x, x2);
	ASSERTG (FFT_state(x) != FULLY_FFTed);
	gwmul3 (gwdata, x, x2, x3, GWMUL_FFT_S1 | GWMUL_FFT_S2);
	ASSERTG (FFT_state(x) == FULLY_FFTed);
	gwmul3 (gwdata, x, x, x, 0);
	squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "gwmul3 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x3, g, "gwmul3 two dest #2");

	gwsquare2 (gwdata, x, x2, 0); gtog (g, g2); squaregi (&gwdata->gdata, g2); specialmodg (gwdata, g2);
	gwsquare2 (gwdata, x2, x3, 0); gtog (g2, g3); squaregi (&gwdata->gdata, g3); specialmodg (gwdata, g3);
	gwaddmul4 (gwdata, x2, x3, x, x4, GWMUL_FFT_S3); gtog (g2, g4); addg (g3, g4); mulg (g, g4); specialmodg (gwdata, g4);
	gwmul3 (gwdata, x, x, x, 0); squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "addmul4 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "addmul4 two dest #2");

	gwsubmul4 (gwdata, x2, x3, x, x4, GWMUL_FFT_S3); gtog (g2, g4); subg (g3, g4); mulg (g, g4); specialmodg (gwdata, g4);
	gwmul3 (gwdata, x, x, x, 0); squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "submul4 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "submul4 two dest #2");

	gwmuladd4 (gwdata, x, x2, x3, x4, GWMUL_FFT_S1); gtog (g2, g4); mulg (g, g4); addg (g3, g4); specialmodg (gwdata, g4);
	gwmul3 (gwdata, x, x, x, 0); squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "muladd4 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "muladd4 two dest #2");

	gwmulsub4 (gwdata, x, x2, x3, x4, GWMUL_FFT_S1); gtog (g2, g4); mulg (g, g4); subg (g3, g4); specialmodg (gwdata, g4);
	gwmul3 (gwdata, x, x, x, 0); squaregi (&gwdata->gdata, g); specialmodg (gwdata, g);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "mulsub4 two dest #1");
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mulsub4 two dest #2");

/* Test both mul-by-const and addin (carefully and not carefully!).  Test addmul and mul add too. */

	gwsetmulbyconst (gwdata, 3);
	gwsetaddin (gwdata, 4);
	gwsetpostmulbyconstaddin (gwdata, 2);

	gwsquare2 (gwdata, x, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	gtog (g, g4); squaregi (&gwdata->gdata, g4); iaddg (4, g4); imulg (3, g4); iaddg (2, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #1");

	gwcopy (gwdata, x, x4);
	gwmul3 (gwdata, x, x4, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #2");

	gwmul3_carefully (gwdata, x, x, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #3");

	gwcopy (gwdata, x, x4);
	gwmul3_carefully (gwdata, x, x4, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #4");

	gwaddmul4 (gwdata, x, x2, x3, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	gtog (g, g4); addg (g2, g4); mulg (g3, g4); iaddg (4, g4); imulg (3, g4); iaddg (2, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #5");

	gwdata->careful_count = 1;
	gwaddmul4 (gwdata, x, x2, x3, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #6");

	gwmuladd4 (gwdata, x, x2, x3, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	gtog (g, g4); mulg (g2, g4); addg (g3, g4); iaddg (4, g4); imulg (3, g4); iaddg (2, g4); specialmodg (gwdata, g4);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #7");

	gwdata->careful_count = 1;
	gwmuladd4 (gwdata, x, x2, x3, x4, GWMUL_MULBYCONST | GWMUL_ADDINCONST);
	if (CHECK_OFTEN) compare_with_text (thread_num, gwdata, x4, g4, "mul by const and addin const #8");

	gwsetaddin (gwdata, 0);
	gwsetpostmulbyconstaddin (gwdata, 0);

/* Do the final compare */

done:	if (!CHECK_OFTEN) compare_with_text (thread_num, gwdata, x, g, "final");
	if (gwdata->GWERROR) OutputBoth (thread_num, "GWERROR set during calculations.\n");

/* Print final stats */

	sprintf (buf, "Test complete. MaxErr=%.10g\n", gw_get_maxerr (gwdata));
	OutputBoth (thread_num, buf);
	OutputBoth (thread_num, "\n");

	pushg (&gwdata->gdata, 4);
}

/* Ramdomly generate k,b,n,c combinations to test.  Results are compared */
/* again giants code performing the same operations. */

int test_randomly (
	int	thread_num,		/* Worker number */
	struct PriorityInfo *sp_info)	/* SetPriority information */
{
	gwhandle gwdata;
	int	kbits, cbits, threads, res, stop_reason;
	double	k;
	unsigned int b;
	int	c;
	unsigned int n;
	char	buf[200], fft_desc[100], SPECIFIC_K[20], SPECIFIC_C[20];
	int	MAX_THREADS, MIN_K_BITS, MAX_K_BITS;
	int	MAX_C_BITS_FOR_SMALL_K, MAX_C_BITS_FOR_LARGE_K;
	int	SPECIFIC_B, SPECIFIC_N, SPECIFIC_L2_CACHE, SPECIFIC_THREADS, SPECIFIC_FFTLEN;
	int	FFT_LIMIT_THRESHOLD;

/* Get control variables */

	MAX_THREADS = IniSectionGetInt (INI_FILE, "QA", "MAX_THREADS", HW_NUM_THREADS);
	if (MAX_THREADS < 1) MAX_THREADS = 1;
//	if (MAX_THREADS > (int) HW_NUM_THREADS) MAX_THREADS = HW_NUM_THREADS;
	MIN_K_BITS = IniSectionGetInt (INI_FILE, "QA", "MIN_K_BITS", 1);
	MAX_K_BITS = IniSectionGetInt (INI_FILE, "QA", "MAX_K_BITS", 49);
	MAX_C_BITS_FOR_SMALL_K = IniSectionGetInt (INI_FILE, "QA", "MAX_C_BITS_FOR_SMALL_K", 4);
	MAX_C_BITS_FOR_LARGE_K = IniSectionGetInt (INI_FILE, "QA", "MAX_C_BITS_FOR_LARGE_K", 20);
	/* Only QA numbers that are within this percent of the FFT limit */
	/* The default is 100% of numbers are tested.  I set this to 1% to */
	/* test numbers near the FFT limits to look for excessive */
	/* round off problems. */
	FFT_LIMIT_THRESHOLD = IniSectionGetInt (INI_FILE, "QA", "FFT_LIMIT_THRESHOLD", 100);

	IniSectionGetString (INI_FILE, "QA", "SPECIFIC_K", SPECIFIC_K, 20, "1");
	SPECIFIC_B = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_B", 2);
	SPECIFIC_N = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_N", 0);
	IniSectionGetString (INI_FILE, "QA", "SPECIFIC_C", SPECIFIC_C, 20, "-1");
	SPECIFIC_FFTLEN = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_FFTLEN", 0);
	SPECIFIC_L2_CACHE = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_L2_CACHE", CPU_L2_CACHE_SIZE);
	SPECIFIC_THREADS = IniSectionGetInt (INI_FILE, "QA", "SPECIFIC_THREADS", 1);

	set_seed (thread_num);
	for ( ; ; ) {

/* Abort loop when requested */

		stop_reason = stopCheck (thread_num);
		if (stop_reason) break;

/* Generate the number to QA */

		if (MAX_K_BITS == MIN_K_BITS) kbits = 0;
		else kbits = (unsigned int) rand () % (MAX_K_BITS - MIN_K_BITS + 1);
		kbits += MIN_K_BITS;
		if (kbits <= 20)
			cbits = (unsigned int) rand () % MAX_C_BITS_FOR_SMALL_K + 1;
		else
			cbits = (unsigned int) rand () % MAX_C_BITS_FOR_LARGE_K + 1;
		k = gen_k (kbits);
		c = gen_c (cbits);
		b = gen_b ();
		n = gen_n ();
		threads = rand () % MAX_THREADS + 1;
		switch (rand () % 5) {
			case 0:	CPU_L2_CACHE_SIZE = 128; break;
			case 1:	CPU_L2_CACHE_SIZE = 256; break;
			case 2:	CPU_L2_CACHE_SIZE = 512; break;
			case 3:	CPU_L2_CACHE_SIZE = 1024; break;
			case 4:	CPU_L2_CACHE_SIZE = -1; break;
		}

/* Override the generated values and use the specified values.  This is */
/* primarily done for debugging. */

		if (SPECIFIC_N) {
			k = atof (SPECIFIC_K);
			b = SPECIFIC_B;
			n = SPECIFIC_N;
			c = atoi (SPECIFIC_C);
			CPU_L2_CACHE_SIZE = SPECIFIC_L2_CACHE;
			threads = SPECIFIC_THREADS;
			FFT_LIMIT_THRESHOLD = 100;
		}

/* Test using the default FFT implementation and comparing it to giants */

again:		gwinit (&gwdata);
		{
			char	numstr[80];
			gw_as_string (numstr, k, b, n, c);
			sprintf (buf, "Trying gwsetup on %s.\n", numstr);
			OutputBoth (thread_num, buf);
		}
//gwdata.force_general_mod = 1;
//static int junk = 0;
//gwset_larger_fftlen_count(&gwdata,junk++);
		gwset_maxmulbyconst (&gwdata, 5);
		if (SPECIFIC_FFTLEN) gwset_safety_margin (&gwdata, -2.0);
		gwset_minimum_fftlen (&gwdata, SPECIFIC_FFTLEN);
		gwset_num_threads (&gwdata, threads);
		gwset_thread_callback (&gwdata, SetAuxThreadPriority);
		gwset_thread_callback_data (&gwdata, sp_info);
		res = gwsetup (&gwdata, k, b, n, c);
		if (res) {
			char	numstr[80];
			gw_as_string (numstr, k, b, n, c);
			sprintf (buf, "Gwsetup failed on %s with error code %d.\n", numstr, res);
			OutputBoth (thread_num, buf);
			continue;
		}
		if (! gwnear_fft_limit (&gwdata, FFT_LIMIT_THRESHOLD)) {
			gwdone (&gwdata);
			continue;
		}

		// Try to QA a rational FFT
		if ((rand () & 7) == 1 && !SPECIFIC_N && !gwdata.RATIONAL_FFT) {
			n = n / gwdata.FFTLEN * gwdata.FFTLEN;		// Make n a multiple of the FFT length
			gwdone (&gwdata);
			if (n == 0) continue;
			goto again;
		}

		// Optional reject testing zero-padded and generic modulo FFTs
		if ((!IniSectionGetInt (INI_FILE, "QA", "TEST_ZERO_PADDED", 1) && gwdata.ZERO_PADDED_FFT) ||
		    (!IniSectionGetInt (INI_FILE, "QA", "TEST_GENERIC_MOD", 1) && gwdata.GENERAL_MOD)) {
			gwdone (&gwdata);
			continue;
		}

		sprintf (buf, "Starting %s QA run on %s, kbits=%d, cbits=%d\n",
			 gwdata.RATIONAL_FFT ? "rational" : "irrational",
			 gwmodulo_as_string (&gwdata), kbits, cbits);
		OutputBoth (thread_num, buf);
		gwfft_description (&gwdata, fft_desc);
		if (gwfftlen (&gwdata) < 140000)
			sprintf (buf,
				 "Using %s, virtual_bits_per_word=%.5g/%.5g\n",
				 fft_desc,
				 virtual_bits_per_word (&gwdata),
				 gwdata.fft_max_bits_per_word);
		else
			sprintf (buf, "Using %s, virtual_bits_per_word=%.5g/%.5g, L2_cache_size=%d\n",
				 fft_desc,
				 virtual_bits_per_word (&gwdata),
				 gwdata.fft_max_bits_per_word,
				 CPU_L2_CACHE_SIZE);
		OutputBoth (thread_num, buf);
		test_it (thread_num, &gwdata);
		gwdone (&gwdata);
	}

/* All done */

	return (stop_reason);
}

/* Ramdomly generate k,b,n,c combinations to test.  Results are compared */
/* using all possible implementations of the FFT (different L2 cache sizes */
/* and non-SSE2). */

int test_all_impl (
	int	thread_num,		/* Worker number */
	struct PriorityInfo *sp_info)	/* SetPriority information */
{
	int	kbits, cbits, threads, stop_reason;
	double	k;
	unsigned int b;
	int	c;
	unsigned int n;
	int	MAX_THREADS, MIN_K_BITS, MAX_K_BITS;
	int	MAX_C_BITS_FOR_SMALL_K, MAX_C_BITS_FOR_LARGE_K;

/* Get control variables */

	MAX_THREADS = IniSectionGetInt (INI_FILE, "QA", "MAX_THREADS", HW_NUM_THREADS);
	if (MAX_THREADS < 1) MAX_THREADS = 1;
//	if (MAX_THREADS > (int) HW_NUM_THREADS) MAX_THREADS = HW_NUM_THREADS;
	MIN_K_BITS = IniSectionGetInt (INI_FILE, "QA", "MIN_K_BITS", 1);
	MAX_K_BITS = IniSectionGetInt (INI_FILE, "QA", "MAX_K_BITS", 49);
	MAX_C_BITS_FOR_SMALL_K = IniSectionGetInt (INI_FILE, "QA", "MAX_C_BITS_FOR_SMALL_K", 4);
	MAX_C_BITS_FOR_LARGE_K = IniSectionGetInt (INI_FILE, "QA", "MAX_C_BITS_FOR_LARGE_K", 20);

	set_seed (thread_num);
	for ( ; ; ) {

/* Abort loop when requested */

		stop_reason = stopCheck (thread_num);
		if (stop_reason) break;

/* Generate the number to QA */

		if (MAX_K_BITS == MIN_K_BITS) kbits = 0;
		else kbits = (unsigned int) rand () % (MAX_K_BITS - MIN_K_BITS + 1);
		kbits += MIN_K_BITS;
		if (kbits <= 20)
			cbits = (unsigned int) rand () % MAX_C_BITS_FOR_SMALL_K + 1;
		else
			cbits = (unsigned int) rand () % MAX_C_BITS_FOR_LARGE_K + 1;
		k = gen_k (kbits);
		c = gen_c (cbits);
		b = gen_b ();
		n = gen_n ();
		threads = rand () % MAX_THREADS + 1;

/* Test using multiple implementations of an FFT size */

		test_it_all (thread_num, sp_info, k, b, n, c, threads);
	}

/* All done */

	return (stop_reason);
}
