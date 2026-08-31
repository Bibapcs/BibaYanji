/*----------------------------------------------------------------------
| gwbench.c
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

/* Include files */

#ifdef __linux__		// Required for CentOS 5 compilation
#define _GNU_SOURCE
#define __USE_UNIX98
#include <sys/mman.h>
#endif
#include <stdio.h>
#include "cpuid.h"
#include "gwnum.h"
#include "gwbench.h"
#include "gwini.h"
#include "gwthread.h"

/* Include the open source SQL database */

//#define SQLITE_OMIT_ALTERTABLE 1
//#define SQLITE_OMIT_ANALYZE 1
//#define SQLITE_OMIT_ATTACH 1
//#define SQLITE_OMIT_AUTHORIZATION 1
//#define SQLITE_OMIT_AUTOINCREMENT 1
//#define SQLITE_OMIT_AUTOINIT 1
////#define SQLITE_OMIT_AUTOMATIC_INDEX 1
////#define SQLITE_OMIT_AUTORESET 1
//#define SQLITE_OMIT_AUTOVACUUM 1
////#define SQLITE_OMIT_BETWEEN_OPTIMIZATION 1
//#define SQLITE_OMIT_BLOB_LITERAL 1
////#define SQLITE_OMIT_BTREECOUNT 1
////#define SQLITE_OMIT_CAST 1
//#define SQLITE_OMIT_CHECK 1
//#define SQLITE_OMIT_COMPILEOPTION_DIAGS 1
//#define SQLITE_OMIT_COMPLETE 1
////#define SQLITE_OMIT_COMPOUND_SELECT 1
////#define SQLITE_OMIT_CTE 1
//#define SQLITE_OMIT_DATETIME_FUNCS 1
//#define SQLITE_OMIT_DECLTYPE 1
//#define SQLITE_OMIT_DEPRECATED 1
//#define SQLITE_OMIT_DISKIO 1
//#define SQLITE_OMIT_EXPLAIN 1
//#define SQLITE_OMIT_FLAG_PRAGMAS 1
////#define SQLITE_OMIT_FLOATING_POINT 1
////#define SQLITE_OMIT_FOREIGN_KEY 1
//#define SQLITE_OMIT_GET_TABLE 1
//#define SQLITE_OMIT_INCRBLOB 1
//#define SQLITE_OMIT_INTEGRITY_CHECK 1
////#define SQLITE_OMIT_LIKE_OPTIMIZATION 1
//#define SQLITE_OMIT_LOAD_EXTENSION 1
//#define SQLITE_OMIT_LOCALTIME 1
//#define SQLITE_OMIT_LOOKASIDE 1
////#define SQLITE_OMIT_MEMORYDB 1
////#define SQLITE_OMIT_OR_OPTIMIZATION 1
//#define SQLITE_OMIT_PAGER_PRAGMAS 1
//#define SQLITE_OMIT_PRAGMA 1
////#define SQLITE_OMIT_PROGRESS_CALLBACK 1
////#define SQLITE_OMIT_QUICKBALANCE 1
//#define SQLITE_OMIT_REINDEX 1
//#define SQLITE_OMIT_SCHEMA_PRAGMAS 1
//#define SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS 1
//#define SQLITE_OMIT_SHARED_CACHE 1
////#define SQLITE_OMIT_SUBQUERY 1
//#define SQLITE_OMIT_TCL_VARIABLE 1
////#define SQLITE_OMIT_TEMPDB 1
//#define SQLITE_OMIT_TRACE 1
//#define SQLITE_OMIT_TRIGGER 1
//#define SQLITE_OMIT_TRUNCATE_OPTIMIZATION 1
//#define SQLITE_OMIT_UTF16 1
//#define SQLITE_OMIT_VACUUM 1
////#define SQLITE_OMIT_VIEW 1
////#define SQLITE_OMIT_VIRTUALTABLE 1
//#define SQLITE_OMIT_WAL 1
////#define SQLITE_OMIT_WSD 1
//#define SQLITE_OMIT_XFER_OPT 1
//#define SQLITE_UNTESTABLE 1
////#define SQLITE_ZERO_MALLOC 1
#include "sqlite3.c"

/* Defines */

/* Global variables */

int	BENCH_DB_INITIALIZED = 0;
gwmutex	SQL_MUTEX;				/* Lock for accessing SQL database */
sqlite3 *BENCH_DB = NULL;			/* SQL database storing the bench data */
int	get_max_sql_stmt_prepared = FALSE;	/* SQL stmt used in get_max_thoughput */
sqlite3_stmt *get_max_sql_stmt;

/* Allow overriding which benchmark data we use to select fastest FFT implementations */
/* These values are read from gwnum.txt.  If set, then gwnum will use benchmarking data */
/* for this number of cores/workers to pick best FFT implementations. */

int	BENCH_NUM_CORES = 0;
int	BENCH_NUM_WORKERS = 0;


/****************************************************************************/
/*          Routines to read and write bench data in INI file               */
/****************************************************************************/

void gwbench_read_data (int cpu_flags)
{
	char	sqlite_file[80];		// Write SQLite database to disk for debugging
	char	gwnum_version_string[10];
	char	cpuid_brand_string[49];
	char	bench_data[250];
	int	i, errcode;
	sqlite3_stmt *sql_stmt;

/* Return if we've already initialized the SQL benchmark database */

	if (BENCH_DB_INITIALIZED) return;
	BENCH_DB_INITIALIZED = 1;

/* Initialize and acquire the lock */

	gwmutex_init (&SQL_MUTEX);
	gwmutex_lock (&SQL_MUTEX);

/* Read in #cores/#workers overrides from gwnum.txt */

	BENCH_NUM_CORES = IniGetInt (GWNUMINI_FILE, "BenchCores", 0);
	BENCH_NUM_WORKERS = IniGetInt (GWNUMINI_FILE, "BenchWorkers", 0);

/* Create the in-memory SQL DB */

	IniGetString (GWNUMINI_FILE, "SQLiteFile", sqlite_file, sizeof (sqlite_file), ":memory:");
	errcode = sqlite3_open_v2 (sqlite_file, &BENCH_DB, SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (errcode != SQLITE_OK) goto db_error;

/* Create the table to hold the bench data */

	errcode = sqlite3_exec (BENCH_DB,
				"CREATE TABLE bench_data (fftlen INT, num_cores INT, num_workers INT, num_hyperthreads INT, \
							  impl INT, bench_date DATE, bench_length INT, throughput REAL)",
				NULL, NULL, NULL);
	if (errcode != SQLITE_OK) goto db_error;

/* Get the gwnum version when the benchmark data was created.  If this does not match the current gwnum version then we must discard some or all of the */
/* benchmark data (and start regenerating using the current gwnum code).  Version 30.16 deleted a lot of SSE2 FFTs, so delete data when upgrading */
/* from 29.2 benchmark data to 30.16 benchmark data on an SSE2 machine. */

	IniGetString (GWNUMINI_FILE, "GwnumVersion", gwnum_version_string, sizeof (gwnum_version_string), NULL);
	if (strcmp (gwnum_version_string, GWNUM_FFT_IMPL_VERSION)) {
		if (strcmp (gwnum_version_string, GWNUM_FFT_IMPL_VERSION_BAD_SSE2)) goto empty_the_db;
		if (! (cpu_flags & (CPU_AVX512F | CPU_FMA3 | CPU_AVX))) goto empty_the_db;
	}

/* Get the CPUID brand string when the benchmark data was created.  If this does not match the current */
/* CPUID brand string as may happen when a local.txt is inadvisably copied to a new computer, then we */
/* must discard the benchmark data (and start regenerating using the new CPU). */

	IniGetString (GWNUMINI_FILE, "CpuBrand", cpuid_brand_string, sizeof (cpuid_brand_string), NULL);
	if (strcmp (cpuid_brand_string, CPU_BRAND)) goto empty_the_db;

/* Prepare a SQL statement to insert benchmark data */

	errcode = sqlite3_prepare_v2 (BENCH_DB, "INSERT INTO bench_data VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)", -1, &sql_stmt, NULL);
	if (errcode != SQLITE_OK) goto stmt_error;

/* Read the existing throughput benchmark data.  Format for benchmark data is: */
/*	BenchData=fftlen,num_cores,num_workers,num_hyperthreads,impl_id,date,bench_length_in_seconds,throughput */

	for (i = 1; ; i++) {
		int	fftlen, bench_length, num_cores, num_workers, num_hyperthreads, impl_id;
		char	fftlen_multiplier, bench_date[80];
		double	throughput;

		IniGetNthString (GWNUMINI_FILE, "BenchData", i, bench_data, sizeof (bench_data), NULL);
		if (bench_data[0] == 0) break;

		sscanf (bench_data, "%d%c,%d,%d,%d,%08X,%10s,%d,%lf",
			&fftlen, &fftlen_multiplier, &num_cores, &num_workers, &num_hyperthreads,
			&impl_id, bench_date, &bench_length, &throughput);
		if (fftlen_multiplier == ',') 
			sscanf (bench_data, "%d,%d,%d,%d,%08X,%10s,%d,%lf",
				&fftlen, &num_cores, &num_workers, &num_hyperthreads,
				&impl_id, bench_date, &bench_length, &throughput);
		if (fftlen_multiplier == 'K' || fftlen_multiplier == 'k') fftlen <<= 10;
		if (fftlen_multiplier == 'M' || fftlen_multiplier == 'm') fftlen <<= 20;

// validate (sanity check) data before writing it

		if (num_cores < 1 || num_workers < 1 || throughput <= 0.0) continue;

// Add the benchmark data to our SQL table

		errcode = sqlite3_bind_int (sql_stmt, 1, fftlen);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_int (sql_stmt, 2, num_cores);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_int (sql_stmt, 3, num_workers);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_int (sql_stmt, 4, num_hyperthreads);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_int (sql_stmt, 5, impl_id);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_text (sql_stmt, 6, bench_date, -1, SQLITE_TRANSIENT);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_int (sql_stmt, 7, bench_length);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_bind_double (sql_stmt, 8, throughput);
		if (errcode != SQLITE_OK) goto stmt_error;

		errcode = sqlite3_step (sql_stmt);
		if (errcode != SQLITE_DONE) goto stmt_error;

		errcode = sqlite3_reset (sql_stmt);
		if (errcode != SQLITE_OK) goto stmt_error;
	}
	sqlite3_finalize (sql_stmt);

/* Create a view to examine the best 3 throughput numbers for each FFT implementation */

empty_the_db:
	errcode = sqlite3_exec (BENCH_DB,
				"CREATE VIEW best3 AS \
					SELECT * FROM bench_data x WHERE rowid IN ( \
						SELECT rowid FROM bench_data y \
						WHERE x.fftlen = y.fftlen AND x.impl = y.impl AND x.num_cores = y.num_cores AND \
						      x.num_workers = y.num_workers AND x.num_hyperthreads = y.num_hyperthreads \
						ORDER BY throughput DESC LIMIT 3)", NULL, NULL, NULL);
	if (errcode != SQLITE_OK) goto db_error;

/* Create a view that averages the best 3 throughput numbers for each FFT implementation */

	errcode = sqlite3_exec (BENCH_DB,
				"CREATE VIEW avgbest3 AS \
					SELECT fftlen, impl, num_cores, num_workers, num_hyperthreads, \
						SUM (throughput * bench_length) / SUM (bench_length) AS avg_throughput \
					FROM best3 GROUP BY fftlen, impl, num_cores, num_workers, num_hyperthreads", NULL, NULL, NULL);
	if (errcode != SQLITE_OK) goto db_error;

/* Create an index for faster access */

	errcode = sqlite3_exec (BENCH_DB, "CREATE INDEX bd_index1 ON bench_data (fftlen, num_workers, impl)", NULL, NULL, NULL);
	if (errcode != SQLITE_OK) goto db_error;

/* Clean up and return */

	gwmutex_unlock (&SQL_MUTEX);
	return;

/* Error returns */

stmt_error:
	sqlite3_finalize (sql_stmt);
db_error:
	sqlite3_close_v2 (BENCH_DB);
	BENCH_DB = NULL;
	gwmutex_unlock (&SQL_MUTEX);
}

/* Write the benchmark data to gwnum.txt */

void gwbench_write_data (void)
{
	char	bench_data[250];
	int	i, errcode;
	sqlite3_stmt *sql_stmt;

/* If we had errors creating the DB, then do not overwrite the existing gwnum.txt file */

	if (BENCH_DB == NULL) return;

/* Obtain the lock to the database */

	gwmutex_lock (&SQL_MUTEX);

/* Write the gwnum version and CPU brand */

	IniDelayWrites (GWNUMINI_FILE);
	IniWriteString (GWNUMINI_FILE, "GwnumVersion", GWNUM_FFT_IMPL_VERSION);
	IniWriteString (GWNUMINI_FILE, "CpuBrand", CPU_BRAND);

	errcode = sqlite3_prepare_v2 (BENCH_DB, "SELECT * FROM bench_data ORDER BY 1,2,3,4,5,6", -1, &sql_stmt, NULL);
	if (errcode != SQLITE_OK) goto stmt_error;

/* Loop writing out the throughput benchmark data.  But first clear out the existing benchmark data.  Format is: */
/*	BenchData=fftlen,num_cores,num_workers,num_hyperthreads,impl_id,date,bench_length_in_seconds,throughput */

	IniWriteNthString (GWNUMINI_FILE, "BenchData", 0, NULL);
	for (i = 1; ; i++) {
		int	fftlen, bench_length, num_cores, num_workers, num_hyperthreads, impl_id;
		const unsigned char *bench_date;
		double	throughput;

		errcode = sqlite3_step (sql_stmt);
		if (errcode == SQLITE_DONE) break;
		if (errcode != SQLITE_ROW) goto stmt_error;

		fftlen = sqlite3_column_int (sql_stmt, 0);
		num_cores = sqlite3_column_int (sql_stmt, 1);
		num_workers = sqlite3_column_int (sql_stmt, 2);
		num_hyperthreads = sqlite3_column_int (sql_stmt, 3);
		impl_id = sqlite3_column_int (sql_stmt, 4);
		bench_date = sqlite3_column_text (sql_stmt, 5);
		bench_length = sqlite3_column_int (sql_stmt, 6);
		throughput = sqlite3_column_double (sql_stmt, 7);

		sprintf (bench_data, "%d%s,%d,%d,%d,%08X,%s,%d,%.2f",
			 (fftlen & 0x3FF) ? fftlen : fftlen >> 10, (fftlen & 0x3FF) ? "" : "K",
			 num_cores, num_workers, num_hyperthreads, impl_id, bench_date, bench_length, throughput);

		IniWriteNthString (GWNUMINI_FILE, "BenchData", i, bench_data);
	}

/* Cleanup and return */

	IniResumeImmediateWrites (GWNUMINI_FILE);
	sqlite3_finalize (sql_stmt);
	gwmutex_unlock (&SQL_MUTEX);
	return;

/* Error returns */

stmt_error:
	sqlite3_finalize (sql_stmt);
	sqlite3_close_v2 (BENCH_DB);
	BENCH_DB = NULL;
	gwmutex_unlock (&SQL_MUTEX);
}

/* Add data to the benchmark database */

void gwbench_add_data (
	gwhandle *gwdata,			/* Handle returned by gwsetup */
	struct gwbench_add_struct *data)	/* Data to add to the database */
{
	int	errcode;
	sqlite3_stmt *sql_stmt;

/* If we had errors creating the DB, then we cannot add to the database */

	if (BENCH_DB == NULL) return;

/* Obtain the lock to the database */

	gwmutex_lock (&SQL_MUTEX);

/* If get_max_thoughput has a SQL statement prepared, the INSERTs below will not auto-commit. */
/* Close the prepared SQL statement, so that newly added data will be used by the next get_max_throughput call. */

	if (get_max_sql_stmt_prepared) {
		get_max_sql_stmt_prepared = FALSE;
		sqlite3_finalize (get_max_sql_stmt);
	}

/* Prepare a SQL statement to insert benchmark data */

	errcode = sqlite3_prepare_v2 (BENCH_DB, "INSERT INTO bench_data VALUES (?1, ?2, ?3, ?4, ?5, date('now'), ?6, ?7)", -1, &sql_stmt, NULL);
	if (errcode != SQLITE_OK) goto stmt_error;

/* Add a database row */

	errcode = sqlite3_bind_int (sql_stmt, 1, gwdata->FFTLEN);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 2, data->num_cores);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 3, data->num_workers);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 4, data->num_hyperthreads);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 5, gwbench_implementation_id (gwdata, data->error_checking));
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 6, data->bench_length);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_double (sql_stmt, 7, data->throughput);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_step (sql_stmt);
	if (errcode != SQLITE_DONE) goto stmt_error;

/* Clean up and return */

	sqlite3_finalize (sql_stmt);
	gwmutex_unlock (&SQL_MUTEX);
	return;

/* Error returns */

stmt_error:
	sqlite3_finalize (sql_stmt);
	gwmutex_unlock (&SQL_MUTEX);
}

/* Generate the "implementation ID" */

int gwbench_implementation_id (
	gwhandle *gwdata,			/* Handle returned by gwsetup */
	int	error_checking)			/* TRUE if benchmark was run with error checking enabled */
{
	int	clm;

	if (gwdata->cpu_flags & CPU_AVX512F) clm = gwdata->PASS1_CACHE_LINES / 8;
	else if (gwdata->cpu_flags & CPU_AVX) clm = gwdata->PASS1_CACHE_LINES / 4;
	else clm = gwdata->PASS1_CACHE_LINES / 2;
	return (internal_implementation_id (gwdata->FFTLEN, gwdata->FFT_TYPE, gwdata->NEGACYCLIC_FFT, gwdata->NO_PREFETCH_FFT,
					    gwdata->IN_PLACE_FFT, error_checking, gwdata->PASS2_SIZE, gwdata->ARCH, clm));
}

int internal_implementation_id (
	int	fftlen,
	int	fft_type,
	int	negacyclic,
	int	no_prefetch,
	int	in_place,
	int	error_check,
	int	pass2_size,
	int	architecture,
	int	clm)
{
	int	pass2_multiplier;		// 10 possible values: 9,12,15,16,20,21,25,28,35,49
	int	pass2_pow2;

/* Compress the data as follows:
	fft_type = 1-bit for negacyclic + 2-bits (home-grown=0, radix-4=1, r4delay=2, r4dwpn=3)  +1 bits for future use
	fft_sub_type = 2-bits (no-prefetching, in-place)  +2 bits for future use
	normalization_variants = 1 bit (error-checking) + 3-bits for future, we might bench zeropad, non-base-2, etc.
	architecture = 3-bits  +1 for future use
	pass2_size = 48 to 25600, or 3-bits plus 1 spare to represent 9,12,15,16,20,25 and 4-bits for * 2^(0-15)
	32/64-bit = 1-bit
	clm = 2-bits (1,2,4) +1 bit for future use */
// BUG - we should consider encoding pass 1 size rather than pass 2 size above.  There are far fewer pass 1 sizes supported.
// If we ever have to ditch old benchmarks, we should do this

	// Map 1,2,4 to 0,1,2
	if (clm <= 1) clm = 0;
	else if (clm == 2) clm = 1;
	else clm = 2;
	// Compress pass2_size
	if (pass2_size == 0) pass2_multiplier = 0;					// One pass FFT is a special case
	else if (pass2_size % 9 == 0) pass2_multiplier = 0, pass2_size /= 9;		// Pass2_size = 9 * 2^x
	else if (pass2_size % 15 == 0) pass2_multiplier = 2, pass2_size /= 15;		// Pass2_size = 15 * 2^x
	else if (pass2_size % 25 == 0) pass2_multiplier = 5, pass2_size /= 25;		// Pass2_size = 25 * 2^x
	else if (pass2_size % 21 == 0) pass2_multiplier = 6, pass2_size /= 21;		// Pass2_size = 21 * 2^x
	else if (pass2_size % 35 == 0) pass2_multiplier = 7, pass2_size /= 35;		// Pass2_size = 35 * 2^x
	else if (pass2_size % 49 == 0) pass2_multiplier = 8, pass2_size /= 49;		// Pass2_size = 49 * 2^x
	else if (pass2_size % 12 == 0) pass2_multiplier = 1, pass2_size /= 12;		// Pass2_size = 12 * 2^x
	else if (pass2_size % 20 == 0) pass2_multiplier = 4, pass2_size /= 20;		// Pass2_size = 20 * 2^x
	else if (pass2_size % 28 == 0) pass2_multiplier = 9, pass2_size /= 28;		// Pass2_size = 28 * 2^x
	else if (pass2_size % 16 == 0) pass2_multiplier = 3, pass2_size /= 16;		// Pass2_size = 16 * 2^x
	else pass2_multiplier = 10;							// Can't happen
	for (pass2_pow2 = 0; pass2_size >= 2; pass2_pow2++, pass2_size >>= 1);
	// Make sure error_check, negacyclic, other flags are one bit
	error_check = !!error_check;
	negacyclic = !!negacyclic;
	no_prefetch = !!no_prefetch;
	in_place = !!in_place;

/* For readability as hex, we try to start values on 4-bit boundaries */

	return ((negacyclic << 27) + (fft_type << 24) +		// Negacyclic (which jmptable to use) and FFT type
		(no_prefetch << 21) + (in_place << 20) +	// FFT sub-type (no_prefetch and in-place)
		(error_check << 16) +				// Normalization options that affected benchmark
		(architecture << 12) +				// CPU architecture
		(pass2_multiplier << 8) +			// Compressed pass 2 size part 1
		(pass2_pow2 << 4) +				// Compressed pass 2 size part 2
#ifndef X86_64
		(1 << 3) +					// True if 32-bit FFT
#endif
		(clm << 0));					// Compressed cache-line-multiplier
}

/* Returns TRUE if the implementation id returned by gwbench_get_max_throughput matches the data parsed from a jmptable entry */

int internal_implementation_ids_match (
	int	impl_id,
	int	fftlen,
	int	fft_type,
	int	no_prefetch,
	int	in_place,
	int	pass2_size,
	int	architecture,
	int	clm)
{
	int	pass2_multiplier;		// 6 possible values: 9,12,15,16,20,25
	int	pass2_pow2;

	// Map 1,2,4 to 0,1,2
	if (clm <= 1) clm = 0;
	else if (clm == 2) clm = 1;
	else clm = 2;
	// Compress pass2_size
	if (pass2_size == 0) pass2_multiplier = 0;					// One pass FFT is a special case
	else if (pass2_size % 9 == 0) pass2_multiplier = 0, pass2_size /= 9;		// Pass2_size = 9 * 2^x
	else if (pass2_size % 15 == 0) pass2_multiplier = 2, pass2_size /= 15;		// Pass2_size = 15 * 2^x
	else if (pass2_size % 25 == 0) pass2_multiplier = 5, pass2_size /= 25;		// Pass2_size = 25 * 2^x
	else if (pass2_size % 21 == 0) pass2_multiplier = 6, pass2_size /= 21;		// Pass2_size = 21 * 2^x
	else if (pass2_size % 35 == 0) pass2_multiplier = 7, pass2_size /= 35;		// Pass2_size = 35 * 2^x
	else if (pass2_size % 49 == 0) pass2_multiplier = 8, pass2_size /= 49;		// Pass2_size = 49 * 2^x
	else if (pass2_size % 12 == 0) pass2_multiplier = 1, pass2_size /= 12;		// Pass2_size = 12 * 2^x
	else if (pass2_size % 20 == 0) pass2_multiplier = 4, pass2_size /= 20;		// Pass2_size = 20 * 2^x
	else if (pass2_size % 28 == 0) pass2_multiplier = 9, pass2_size /= 28;		// Pass2_size = 28 * 2^x
	else if (pass2_size % 16 == 0) pass2_multiplier = 3, pass2_size /= 16;		// Pass2_size = 16 * 2^x
	else pass2_multiplier = 10;							// Can't happen
	for (pass2_pow2 = 0; pass2_size >= 2; pass2_pow2++, pass2_size >>= 1);
	// Strip negacyclic, error-check, 32-bit flags from implementation id -- gwbench_get_max_throughput will have made sure these match.
	impl_id &= ~0x8010008;
	// Make sure flags are one bit
	no_prefetch = !!no_prefetch;
	in_place = !!in_place;
	// Return true if this is a match
	return (impl_id == (fft_type << 24) + (no_prefetch << 21) + (in_place << 20) +
			   (architecture << 12) + (pass2_multiplier << 8) + (pass2_pow2 << 4) + (clm << 0));
}

/* This routine returns the implementation ID and throughput for the best implementation of this FFT length. */
/* If throughput for this FFT length is not in the benchmark database, then -1.0 is returned */

void gwbench_get_max_throughput (
	int	fftlen,				/* FFT length to get bench data on */
	int	arch,				/* Return bench data where this CPU architecture was used */
	int	num_cores,			/* Return bench data where this number of cores were used */
	int	num_workers,			/* Return bench data where this number of workers were used */
	int	num_hyperthreads,		/* Return bench data where this number of hyperthreads were used */
	int	negacyclic,			/* TRUE if negacyclic FFT bench data should be returned */
	int	error_check,			/* TRUE if error_checking bench data should be returned */
	int	*impl,				/* Implementation ID of best FFT implementation */
	double	*throughput)			/* Throughput of best FFT implementation (or -1 if cannot be determined) */
{
	int	errcode, impl_bits, min_arch, max_arch;

/* Assume we will fail to get throughput data */

	*impl = -1;
	*throughput = -1.0;

/* Apply overrides from gwnum.txt */

	if (BENCH_NUM_CORES) num_cores = BENCH_NUM_CORES;
	if (BENCH_NUM_WORKERS) num_workers = BENCH_NUM_WORKERS;

/* If errors occured reading bench DB, then return dont-know-the-fastest-implementation */

	if (BENCH_DB == NULL) return;

/* Obtain the lock to the database */

	gwmutex_lock (&SQL_MUTEX);

/* Calculate the implentation ID bits that we must match */

#ifndef X86_64
	impl_bits = 0x8;		// 32-bit FFT bench data desired
#else
	impl_bits = 0;			// 64-bit FFT bench data desired
#endif
	if (negacyclic) impl_bits |= 0x8000000;
	if (error_check) impl_bits |= 0x10000;

/* We really made a mess here.  What we really want is the best implementation for the jmptable we are using. */
/* Unfortunately, we decided to write the architecture value to the benchmark data in gwnum.txt.  In version 29.5 */
/* and earlier we searched for the matching architecture I believe in an effort to protect us from a user copying */
/* benchmark data from an incompatible machine or running benchmarks with CPU_FLAGS overridden.  This was a poor solution */
/* as one of our Pentium 4 users found the arch=1, arch=2, or arch=3 could be the best implementation.  One possible remedy is */
/* to eliminate the arch= check.  This fails in the case of an AVX-512 user upgrading to version 29.6.  The user could have */
/* a gwnum.txt file full of benchmark data for AVX FFTs (yjmptable) but we are searching the zjmptable.  The other possible */
/* remedy is to map the incoming arch value into all the possible arch values that are valid for that jmptable.  Alas, */
/* arch=3 (CORE) is used for both the xjmptable and yjmptable.  So, we're going with a hybrid approach and if arch=3 we'll */
/* just hope that gwnum.txt is only populated with valid benchmark data for this machine. */

	min_arch = 0, max_arch = 15;	// This should always get overwritten

	//BUG - we should pass in the jmptable we are using (and AMD flag) rather than an architecture value.
	// If we did that we would not have the arch=3 problem below.

	// From mult.asm 0=BLEND, 1=Pentium-4 w/ TLB prefetch, 2=Pentium-4, 3=CORE are valid xjmptable architectures
	if (arch >= 0 && arch <= 2) min_arch = 0, max_arch = 3;
	// From mult.asm 5=K8, 6=K10 are valid xjmptable architectures for AMD
	// Accept arch=0 to 6 benchmarks and hope no FMA3 (arch=4) values for yjmptable are erroneously in gwnum.txt
	if (arch >= 5 && arch <= 6) min_arch = 0, max_arch = 6;
	// From mult.asm 3=CORE, 4=FMA3 are valid yjmptable architectures, but since arch=3
	// input value could be xjmptable, select a wide range of arch values from gwnum.txt
	if (arch == 3) min_arch = 0, max_arch = 4;
	if (arch == 4) min_arch = 3, max_arch = 4;
	// From mult.asm 8=SKX is the only valid zjmptable architecture
	if (arch == 8) min_arch = 8, max_arch = 8;

	min_arch = min_arch << 12;
	max_arch = max_arch << 12;

/* Prepare a SQL statement to get benchmark data */

	if (!get_max_sql_stmt_prepared) {
		errcode = sqlite3_prepare_v2 (BENCH_DB, "SELECT impl, avg_throughput FROM avgbest3 \
							 WHERE fftlen = ?1 AND num_cores = ?2 AND num_workers = ?3 AND \
								num_hyperthreads = ?4 AND (impl & 0x8010008) = ?5 AND \
								(impl & 0xF000) BETWEEN ?6 AND ?7 \
							 ORDER BY avg_throughput DESC LIMIT 1", -1, &get_max_sql_stmt, NULL);
		if (errcode != SQLITE_OK) goto stmt_error;
		get_max_sql_stmt_prepared = TRUE;
	}

/* Get the throughput data (if any) */

	errcode = sqlite3_bind_int (get_max_sql_stmt, 1, fftlen);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 2, num_cores);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 3, num_workers);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 4, num_hyperthreads);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 5, impl_bits);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 6, min_arch);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (get_max_sql_stmt, 7, max_arch);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_step (get_max_sql_stmt);
	if (errcode == SQLITE_ROW) {
		*impl = sqlite3_column_int (get_max_sql_stmt, 0);
		*throughput = sqlite3_column_double (get_max_sql_stmt, 1);
	}
	else if (errcode != SQLITE_DONE) goto stmt_error;

/* Clean up and return */

	sqlite3_reset (get_max_sql_stmt);
	gwmutex_unlock (&SQL_MUTEX);
	return;

/* Error returns */

stmt_error:
	sqlite3_finalize (get_max_sql_stmt);
	get_max_sql_stmt_prepared = FALSE;
	gwmutex_unlock (&SQL_MUTEX);
	return;
}

/* This routine lets the caller ask how many benchmarks we already have for testing k*b^n+c.  If caller decides */
/* more benchmarks would be desirable, the FFT length is returned for caller to run a throughput benchmark. */

void gwbench_get_num_benchmarks (
	double k,
	unsigned long b,
	unsigned long n,
	signed long c,
	unsigned long minimum_fftlen,
	int	num_cores,
	int	num_workers,
	int	hyperthreading,
	int	error_check,
	unsigned long *fftlen,
	int	*negacyclic,
	int	*num_benchmarks)
{
	gwhandle gwdata;			/* Temporary gwnum handle */
	sqlite3_stmt *sql_stmt;
	int	errcode, impl_bits, count, impls;

/* Return dummy data if we cannot get the number of benchmarks */

	*fftlen = 0;
	*num_benchmarks = 9999;

/* If bench DB not initialized or errors occured reading bench DB, then return */

	if (!BENCH_DB_INITIALIZED) return;
	if (BENCH_DB == NULL) return;

/* Get info on smallest possible FFT length for this k*b^n+c. */

	gwinit (&gwdata);
	gwclear_use_benchmarks (&gwdata);
	gwdata.minimum_fftlen = minimum_fftlen;
	gwdata.bench_num_cores = num_cores;
	gwdata.bench_num_workers = num_workers;
	gwdata.will_hyperthread = hyperthreading;
	gwdata.bench_pick_nth_fft = 1;				// This forces smallest usable FFT length to be returned
	if (gwinfo (&gwdata, k, b, n, c)) return;		// Return if k*b^n+c is untestable
	*fftlen = gwdata.jmptab->fftlen;			// Return FFT length that might need benchmarking
	*negacyclic = gwdata.NEGACYCLIC_FFT;

/* Query the database for how many benchmarks we have for this FFT length. */

/* Calculate the implentation ID bits that we must match */

#ifndef X86_64
	impl_bits = 0x8;		// 32-bit FFT bench data desired
#else
	impl_bits = 0;			// 64-bit FFT bench data desired
#endif
	if (gwdata.NEGACYCLIC_FFT) impl_bits |= 0x8000000;
	if (error_check) impl_bits |= 0x10000;

/* Obtain the lock to the database */

	gwmutex_lock (&SQL_MUTEX);

/* Prepare a SQL statement to get benchmark data */
/* We'll assume that the average number of benchmarks per implementation is good enough for the caller. */
/* This will be the case if caller benchmarks all implementations.  A hand edited gwnum.txt or repeated */
/* interrupting of benchmarks before all implmentations are benchmarked could throw this average off. */

	errcode = sqlite3_prepare_v2 (BENCH_DB, "SELECT COUNT(*), COUNT (DISTINCT IMPL) FROM bench_data \
						 WHERE fftlen = ?1 AND num_cores = ?2 AND num_workers = ?3 AND \
						 num_hyperthreads = ?4 AND (impl & 0x8010008) = ?5", -1, &sql_stmt, NULL);
	if (errcode != SQLITE_OK) goto stmt_error;

/* Get the throughput data (if any) */

	errcode = sqlite3_bind_int (sql_stmt, 1, gwdata.jmptab->fftlen);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 2, num_cores);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 3, num_workers);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 4, hyperthreading ? 2 : 1);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_bind_int (sql_stmt, 5, impl_bits);
	if (errcode != SQLITE_OK) goto stmt_error;

	errcode = sqlite3_step (sql_stmt);
	if (errcode == SQLITE_ROW) {
		count = sqlite3_column_int (sql_stmt, 0);
		impls = sqlite3_column_int (sql_stmt, 1);
	}
	else if (errcode != SQLITE_DONE) goto stmt_error;
	sqlite3_reset (sql_stmt);

/* Return this FFT length's number of benchmarks. */

	if (impls) *num_benchmarks = count / impls;

/* Clean up and return */

stmt_error:
	sqlite3_finalize (sql_stmt);
	gwmutex_unlock (&SQL_MUTEX);
}
