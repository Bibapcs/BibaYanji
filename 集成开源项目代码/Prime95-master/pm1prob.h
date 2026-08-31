/* Copied with permission from https://github.com/preda/gpuowl/pm1 on 2020-08-11 */
/* Code courtesy of Mihai Preda */

#ifndef _PM1PROB_H
#define _PM1PROB_H

/* This is used by C and C++ code.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

// Returns the probability of PM1(B1,B2) success for a Mersenne 2^exponent -1 already TF'ed to factoredUpTo.
double pm1prob (double takeAwayBits, double factoredUpTo, uint64_t B1, uint64_t B2);

// Dickman's "rho" function; rho(x) == F(1/x)
double rho (double x);

#ifdef __cplusplus
}
#endif

#endif
