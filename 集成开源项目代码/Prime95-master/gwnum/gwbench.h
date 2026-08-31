/*----------------------------------------------------------------------
| gwbench.h
|
| This file contains routines to read, write, and use benchmarking data stored
| in the gwnum.txt INI file.
|
| Benchmarking data lets us select the fastest FFT implementation for every CPU.
| By default, gwnum picks a default FFT implementation based on the CPU architecture.
| We can fine-tune performance by benchmarking the different FFT implementations
| on the end user machine looking for an FFT implementation that is faster than the
| default selection.
|
| Copyright 2017-2024 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWBENCH_H
#define _GWBENCH_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Defines */

#define GWNUMINI_FILE	"gwnum.txt"			/* Name of the INI file */

#define GWNUM_FFT_IMPL_VERSION		"30.16"		/* This version number changes whenever FFT implementations change - meaning */
							/* we need to toss benchmarking data from older gwnum versions. */
#define GWNUM_FFT_IMPL_VERSION_BAD_SSE2	"29.2"		/* This version number is compatible with current version except for SSE2 data */

/******************************************************************************
*                                 Globals                                     *
******************************************************************************/

/* Benchmarking global variables (only set after first gwinit call). */
/* These values are read from gwnum.txt.  If set, then gwnum uses benchmarking data */
/* for this number of cores and workers to pick best FFT implementations. */
/* You might check these values to run throughput benchmarks for this combination */

extern int BENCH_NUM_CORES;		/* Override from gwnum.txt. Use this #cores for benchmark FFT selection */
extern int BENCH_NUM_WORKERS;		/* Override from gwnum.txt. Use this #workers for benchmark FFT selection */

/******************************************************************************
*                                 Routines                                    *
******************************************************************************/

#define GWBENCH_ADD_VERSION		1
struct gwbench_add_struct {
	int	version;		/* version number for this structure */
	double	throughput;		/* throughput in squarings per second */
	double	bench_length;		/* time (in seconds) the benchmark was run */
	int	num_cores;		/* number of cores kept busy by the benchmark */
	int	num_workers;		/* number of workers used by the benchmark */
	int	num_hyperthreads;	/* hyperthreading */
	int	error_checking;		/* benchmark was run with error checking enabled */
};
void gwbench_add_data (gwhandle *, struct gwbench_add_struct *);
void gwbench_write_data (void);
void gwbench_get_num_benchmarks (double, unsigned long, unsigned long, signed long, unsigned long, int, int, int, int, unsigned long *, int *, int *);

/******************************************************************************
*                             Internal Routines                               *
******************************************************************************/

void gwbench_read_data (int);
int gwbench_implementation_id (gwhandle *, int);
int internal_implementation_id (int, int, int, int, int, int, int, int, int);
int internal_implementation_ids_match (int, int, int, int, int, int, int, int);
void gwbench_get_max_throughput (int, int, int, int, int, int, int, int *, double *);

#ifdef __cplusplus
}
#endif

#endif
