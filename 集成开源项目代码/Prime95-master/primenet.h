/*
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) 1997-2021 Mersenne Research, Inc. All Rights Reserved.
//
*/

/* Include file for primenet communication code */

/* The current version number of the the PRIMENET.DLL API */

#define	PRIMENET_VERSION			5
#define	PRIMENET_TRANSACTION_API_VERSION	0.95

/* Initialization routine.  Call from the main thread at startup. */

void LoadPrimenet ();

/* There is one routine used to communicate with the Primenet server */
/* This routine, "PrimeNet", takes two arguments one is an operation_type */
/* defined below, and the other argument is a pointer to a structure. */
/* A different structure is declared for each operation type.  All strings */
/* in structures are zero-terminated. */

int PRIMENET (short operation, void *pkt);

/* Operations as defined in the primenet protocol document */

#define PRIMENET_UPDATE_COMPUTER_INFO	100
#define PRIMENET_PROGRAM_OPTIONS	101
#define PRIMENET_GET_ASSIGNMENT		102
#define PRIMENET_REGISTER_ASSIGNMENT	103
#define PRIMENET_ASSIGNMENT_PROGRESS	104
#define PRIMENET_ASSIGNMENT_RESULT	105
#define PRIMENET_ASSIGNMENT_UNRESERVE	106
#define PRIMENET_BENCHMARK_DATA		107
#define PRIMENET_PING_SERVER		108

/* WARNING, WARNING, WARNING:  Prime95 writes some of these structures */
/* (primenetAssignmentResult, primenetAssignmentUnreserve, */
/* primenetBenchmarkData) out to the prime.spl spool file.  Thus, these */
/* structures must be padded and use data types that are the same size for */
/* all compilers.  This is the only way we can make the spool file portable. */

/* This structure is passed for the uc - Update Computer Info call */

struct primenetUpdateComputerInfo {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	hardware_guid[33];
	char	windows_guid[33];
	char	application[65];	/* Ex. Windows,Prime95,v25.5,build 1 */
	char	cpu_model[65];		/* Ex. Intel(R) Pentium 4 2.6 GHz */
	char	cpu_features[65];	/* Ex. RDTSC, MMX, SSE, SSE2 */
	int32_t	L1_cache_size;		/* Cache size in KB */
	int32_t	L2_cache_size;		/* Cache size in KB */
	int32_t	L3_cache_size;		/* Cache size in KB */
	uint32_t num_cpus;		/* Number of cpus/cores in machine */
	uint32_t num_hyperthread;	/* Number virtual cpus per CPU core */
	uint32_t mem_installed;		/* Physical memory in MB */
	uint32_t cpu_speed;		/* CPU speed in MHz */
	uint32_t hours_per_day;		/* Hours per day the computer runs */
	uint32_t rolling_average;	/* Rough measure of this computer's */
					/* actual output vs. expected output */
					/* <1000 = less actual output */
					/* >1000 = more actual output */
	char	user_id[21];
	char	computer_name[21];

	/* Returned by the server */

	char	user_name[33];
	char	pad[1];			/* For 4-byte alignment */
	uint32_t options_counter;	/* Number of times options have been */
					/* updated on the server. Whenever */
					/* this increases client must get */
					/* program options. */
};


/* This structure is passed for the po - Program Options call */

/* Valid work_preference values */
#define PRIMENET_WP_WHATEVER		0	/* Whatever makes most sense */
#define PRIMENET_WP_FACTOR_LMH		1	/* Factor big numbers to low limits */
#define PRIMENET_WP_FACTOR		2	/* Trial factoring */
#define PRIMENET_WP_PMINUS1		3	/* P-1 of small Mersennes --- not supported */
#define PRIMENET_WP_PFACTOR		4	/* P-1 of large Mersennes */
#define PRIMENET_WP_ECM_SMALL		5	/* ECM of small Mersennes looking for first factors */
#define PRIMENET_WP_ECM_FERMAT		6	/* ECM of Fermat numbers */
#define PRIMENET_WP_ECM_CUNNINGHAM	7	/* ECM of Cunningham numbers --- not supported */
#define PRIMENET_WP_ECM_COFACTOR	8	/* ECM of Mersenne cofactors */
#define PRIMENET_WP_LL_FIRST		100	/* LL first time tests */
#define PRIMENET_WP_LL_DBLCHK		101	/* LL double checks */
#define PRIMENET_WP_LL_WORLD_RECORD	102	/* LL test of world record Mersenne */
#define PRIMENET_WP_LL_100M		104	/* LL 100 million digit */
#define PRIMENET_WP_PRP_FIRST		150	/* PRP test of big Mersennes */
#define PRIMENET_WP_PRP_DBLCHK		151	/* PRP double checks */
#define PRIMENET_WP_PRP_WORLD_RECORD	152	/* PRP test of world record Mersennes */
#define PRIMENET_WP_PRP_100M		153	/* PRP test of 100M digit Mersennes */
#define PRIMENET_WP_PRP_NO_PMINUS1	154	/* PRP test that if possible also needs P-1 */
#define PRIMENET_WP_PRP_DC_PROOF	155	/* PRP double-check where a proof will be produced */
#define PRIMENET_WP_PRP_COFACTOR	160	/* PRP test of Mersenne cofactors */
#define PRIMENET_WP_PRP_COFACTOR_DBLCHK	161	/* PRP double check of Mersenne cofactors */

/* Obsolete work preferences */
#define PRIMENET_WP_LL_10M		103	/* LL 10 million digit --- no longer supported */
#define PRIMENET_WP_LL_FIRST_NOFAC	105	/* LL first time tests, no trial factoring or P-1 factoring -- superfluous */

struct primenetProgramOptions {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	pad[3];
	int32_t cpu_num;		/* CPU number (-1 = all) */

	/* Read/write parameters.  Minus 1 is used to specify parameter */
	/* not passed in. */

	int32_t	num_workers;
	int32_t work_preference;	/* Primenet work preference */
	int32_t priority;
	int32_t daysOfWork;
	int32_t dayMemory;
	int32_t nightMemory;
	int32_t dayStartTime;
	int32_t nightStartTime;
	int32_t runOnBattery;

	/* Returned by the server */
	
	uint32_t options_counter;	/* Number of times options have been */
					/* updated on the server. Whenever */
					/* this increases client must get */
					/* program options. */
};


/* This structure is passed for the ga - Get Assignment call */

/* Valid work_types returned by ga */
#define PRIMENET_WORK_TYPE_FACTOR	2
#define PRIMENET_WORK_TYPE_PMINUS1	3
#define PRIMENET_WORK_TYPE_PFACTOR	4
#define PRIMENET_WORK_TYPE_ECM		5
#define PRIMENET_WORK_TYPE_PPLUS1	6		/* Not yet supported by the server */
#define PRIMENET_WORK_TYPE_FIRST_LL	100
#define PRIMENET_WORK_TYPE_DBLCHK	101
#define PRIMENET_WORK_TYPE_PRP		150
#define PRIMENET_WORK_TYPE_CERT		200

struct primenetGetAssignment {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	pad[3];
	uint32_t cpu_num;		/* CPU number */
	int	get_cert_work;		/* If we are trying to get some certification work, this is set to CertDailyCPULimit */
					/* so server can steer bigger cert jobs to clients willing to do them. */
	float	temp_disk_space;	/* If we are trying to get some first-time / double-check work, this is set to the */
					/* available temp disk space so server can make sure client will be using an adequate proof power */
	uint32_t min_exp;		/* Optional minimum exponent */
	uint32_t max_exp;		/* Optional minimum exponent */

	/* Returned by the server */

	char	assignment_uid[33];
	char	pad2[7];
	uint32_t work_type;
	double	k;			/* K in k*b^n+c */
	uint32_t b;			/* B in k*b^n+c */
	uint32_t n;			/* N in k*b^n+c */
	int32_t	c;			/* C in k*b^n+c */
	uint32_t has_been_pminus1ed;	/* TRUE if P-1 has been run */
	double	how_far_factored;	/* Log base 2 of highest trial factor tested */
	double	factor_to;		/* Log base 2 of how far to trial factor */
	uint64_t B1;			/* P-1/ECM B1 */
	uint64_t B2;			/* P-1/ECM B2 */
	double	tests_saved;		/* Primality tests saved if P-1 finds a factor */
	uint32_t curves;		/* ECM curves to run */
	uint32_t prp_base;		/* PRP base to use in a PRP double-check */
	uint32_t prp_residue_type;	/* PRP residue type to return in a PRP double-check */
	uint32_t prp_dblchk;		/* True is this is a PRP double-check */
	uint32_t num_squarings;		/* Certification number of squarings */
	uint32_t nth_run;		/* P+1 nth run:  1=2/7, 2=6/5, 3=random */
	char	known_factors[2000];	/* List of known factors */
};


/* This structure is passed for the ra - Register Assignment call */

struct primenetRegisterAssignment {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	pad[3];
	uint32_t cpu_num;		/* CPU number */
	uint32_t work_type;
	double	k;			/* K in k*b^n+c */
	uint32_t b;			/* B in k*b^n+c */
	uint32_t n;			/* N in k*b^n+c */
	int32_t	c;			/* C in k*b^n+c */
	uint32_t has_been_pminus1ed;	/* TRUE if P-1 has been run */
	double	how_far_factored;	/* Log base 2 of highest trial */
					/* factor tested. */
	double	factor_to;		/* Log base 2 of how far to trial */
					/* factor. */
	uint64_t B1;			/* P-1/ECM B1 */
	uint64_t B2;			/* P-1/ECM B2 */
	double	tests_saved;		/* Primality tests saved if P-1 */
					/* finds a factor */
	uint32_t curves;		/* ECM curves to run */

	/* Returned by the server */
	
	char	assignment_uid[33];
	char	pad2[3];
};


/* This structure is passed for the ap - Assignment Progress call */

struct primenetAssignmentProgress {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	assignment_uid[33];
	char	stage[11];		/* Stage for multi-part assignments */
	char	pad[3];
	uint32_t cpu_num;		/* CPU number */
	double	pct_complete;		/* From zero to 100 */
	uint32_t end_date;		/* Expected completion time in sec. */
	uint32_t next_update;		/* Next update time in seconds */
	uint32_t fftlen;		/* FFT len being used */
	uint32_t iteration;		/* LL/PRP iteration (zero-based) */
	char	residue[17];		/* LL/PRP interim residue */
	char	error_count[9];		/* LL/PRP interim error count */
};


/* This structure is passed for the ar - Assignment Result call */

#define PRIMENET_AR_NO_RESULT	0	/* No result, just sending done msg */
#define PRIMENET_AR_TF_FACTOR	1	/* Trial factoring, factor found */
#define PRIMENET_AR_P1_FACTOR	2	/* P-1, factor found */
#define PRIMENET_AR_ECM_FACTOR	3	/* ECM, factor found */
#define PRIMENET_AR_TF_NOFACTOR	4	/* Trial Factoring no factor found */
#define PRIMENET_AR_P1_NOFACTOR	5	/* P-1 factoring no factor found */
#define PRIMENET_AR_ECM_NOFACTOR 6	/* ECM factoring no factor found */
#define PRIMENET_AR_PP1_FACTOR	7	/* P+1, factor found */
#define PRIMENET_AR_PP1_NOFACTOR 8	/* P+1 factoring no factor found */
#define PRIMENET_AR_LL_RESULT	100	/* LL result, not prime */
#define PRIMENET_AR_LL_PRIME	101	/* LL result, Mersenne prime */
#define PRIMENET_AR_PRP_RESULT	150	/* PRP result, not prime */
#define PRIMENET_AR_PRP_PRIME	151	/* PRP result, probably prime */
#define PRIMENET_AR_CERT	200	/* Certification result */

// There are (at least) 5 PRP residue types for testing N=(k*b^n+c)/d:
#define	PRIMENET_PRP_TYPE_FERMAT	1	// Fermat PRP.  Calculate a^(N-1) mod N.  PRP if result = 1
#define	PRIMENET_PRP_TYPE_SPRP		2	// SPRP variant.  Calculate a^((N-1)/2) mod N.  PRP if result = +/-1
#define	PRIMENET_PRP_TYPE_FERMAT_VAR	3	// Type 1 variant,b=2,d=1. Calculate a^(N-c) mod N.  PRP if result = a^-(c-1)
#define	PRIMENET_PRP_TYPE_SPRP_VAR	4	// Type 2 variant,b=2,d=1. Calculate a^((N-c)/2) mod N.  PRP if result = +/-a^-((c-1)/2)
#define	PRIMENET_PRP_TYPE_COFACTOR	5	// Cofactor variant.  Calculate a^(N*d-1) mod N*d.  PRP if result = a^(d-1) mod N
// Primenet encourages programs to return type 1 PRP residues as that has been the standard for prime95, PFGW, LLR for many years.
// Primenet encourages programs to return type 5 PRP residues for cofactor tests as that allows Gerbicz-error checking and proofs.

struct primenetAssignmentResult {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	assignment_uid[33];
	char	message[201];		/* Result expressed as string */
	char	pad[1];
	uint32_t done;			/* True is assignment complete */
	uint32_t result_type;		/* Result type defined above */
	double	k;			/* K in k*b^n+c */
	uint32_t b;			/* B in k*b^n+c */
	uint32_t n;			/* N in k*b^n+c */
	int32_t	c;			/* C in k*b^n+c */
	uint32_t curves;		/* ECM curves ran */
	double	start_bits;		/* Log base 2 of starting trial factor tested. */
	double	end_bits;		/* Log base 2 of ending trial factor tested. */
	uint64_t B1;			/* P-1/ECM B1 */
	uint64_t B2;			/* P-1/ECM B2 */
	uint32_t stage;			/* ECM stage the factor was found */
	uint32_t shift_count;		/* LL or PRP shift count */
	uint32_t fftlen;		/* FFT length used, for proper CPU credit on server */
	char	residue[17];		/* LL or PRP residue result */
	char	error_count[9];		/* LL or PRP result error count */
	char	factor[201];		/* Factor found */
	char	cert_hash[65];		/* Certification's 256-bit SHA-3 hash */
	char	proof_hash[33];		/* Proof file's 128-bit MD5 hash */
	char	pad2[3];
	uint32_t num_known_factors;	/* Num known factors used in a PRP test */
	uint32_t gerbicz;		/* TRUE if Gerbicz error checking used in PRP test */
	uint32_t prp_base;		/* Base used in a PRP test */
	uint32_t prp_residue_type;	/* PRP Residue type */
	uint32_t proof_power;		/* Zero if no proof, else power used in proof */
	char	JSONmessage[2000];	/* JSON message.  If not empty, this text is sent rather than the 201-byte message. */
	uint32_t pp1_numerator;		/* P+1 starting numerator */
	uint32_t pp1_denominator;	/* P+1 starting denominator */
/* ADD NEW FIELDS HERE -- ELSE SPOOL FILES WILL BE CORRUPTED WHEN UPGRADING */

	/* Returned by the server */

};


/* This structure is passed for the au - Assignment Unreserve call */

struct primenetAssignmentUnreserve {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	assignment_uid[33];
	char	pad[2];
};


/* This structure is passed for the bd - Benchmark Data call */

#define PRIMENET_BENCH_MAX_DATAPOINTS	50

struct primenetBenchmarkData {
	int32_t	versionNumber;
	char	computer_guid[33];
	char	user_comment[201];	/* User supplied comment */
	char	pad[6];
	uint32_t num_data_points;
	struct {
		char bench[13];		/* What was benchmarked */
		char pad2[3];		/* Align on 8 byte boundary */
		double timing;		/* Time it took (in seconds) */ 
	} data_points[PRIMENET_BENCH_MAX_DATAPOINTS];

	/* Returned by the server */
	
};


/* This structure is passed for the ps - Ping Server call */

struct primenetPingServer {
	int32_t	versionNumber;
	uint32_t ping_type;

	/* Returned by the server */
	
	char	ping_response[513];
	char	pad[3];
};


/* Error codes returned to client */

#define PRIMENET_ERROR_OK		0	/* no error */
#define PRIMENET_ERROR_SERVER_BUSY	3	/* server is too busy now */
#define PRIMENET_ERROR_INVALID_VERSION	4
#define PRIMENET_ERROR_INVALID_TRANSACTION 5
#define PRIMENET_ERROR_INVALID_PARAMETER 7
#define PRIMENET_ERROR_ACCESS_DENIED	9
#define PRIMENET_ERROR_DATABASE_CORRUPT	11
#define PRIMENET_ERROR_DATABASE_FULL_OR_BROKEN	13
#define PRIMENET_ERROR_INVALID_USER	21
#define PRIMENET_ERROR_UNREGISTERED_CPU	30
#define PRIMENET_ERROR_OBSOLETE_CLIENT	31
#define PRIMENET_ERROR_STALE_CPU_INFO	32
#define PRIMENET_ERROR_CPU_IDENTITY_MISMATCH 33
#define PRIMENET_ERROR_CPU_CONFIGURATION_MISMATCH 34
#define PRIMENET_ERROR_NO_ASSIGNMENT	40
#define PRIMENET_ERROR_INVALID_ASSIGNMENT_KEY 43
#define PRIMENET_ERROR_INVALID_ASSIGNMENT_TYPE 44
#define PRIMENET_ERROR_INVALID_RESULT_TYPE 45
#define PRIMENET_ERROR_INVALID_WORK_TYPE 46
#define PRIMENET_ERROR_WORK_NO_LONGER_NEEDED 47

/* These error codes are not returned by the server but are generated */
/* by the client code that communicates with the server. */

#define PRIMENET_NO_ERROR		0
#define PRIMENET_FIRST_INTERNAL_ERROR	1000

#define PRIMENET_ERROR_CONNECT_FAILED	1000
#define PRIMENET_ERROR_SEND_FAILED	1001
#define PRIMENET_ERROR_RECV_FAILED	1002
#define PRIMENET_ERROR_SERVER_UNSPEC	1003
#define PRIMENET_ERROR_PNERRORRESULT	1004
#define PRIMENET_ERROR_PNERRORDETAIL	1005

#define PRIMENET_ERROR_CURL_INIT	1100
#define PRIMENET_ERROR_CURL_PERFORM	1101

#define PRIMENET_ERROR_MODEM_OFF	2000

