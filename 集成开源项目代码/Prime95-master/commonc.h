/* Copyright 1995-2024 Mersenne Research, Inc.  All rights reserved */

#ifndef _COMMONC_H
#define _COMMONC_H

/* This is used by C and C++ code.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */

#define VERSION		"30.19"
#define BUILD_NUM	"20"
/* The list of assigned OS ports follows: */
/* Win9x (prime95) #1 */
/* Linux (mprime)  #2 */
/* Solaris	   #3 (never happened) */
/* Win 64-bit	   #4 */
/* WinNT (ntprime) #5 */
/* FreeBSD(mprime) #6 */
/* OS/2		   #7 */
/* Linux x86-64	   #8 */
/* Mac OS X	   #9 */
/* Mac OS X 64-bit #10 */
/* Haiku	   #11 */
/* FreeBSD 64-bit  #12 */

#define MIN_PRIME	5L		/* Smallest testable prime */
#define MAX_FACTOR	1500000000	/* Largest factorable Mersenne number */
#define MAX_FACTOR_BITS	87		/* Largest factor to bits allowed */
#define ERROR_RATE	0.018		/* Estimated LL error rate on clean run */
#define PRP_ERROR_RATE	0.0001		/* Estimated PRP error rate (assumes Gerbicz error-checking) */

/* Hopefully, hwloc has no limitations regarding setting affinity.  Due to */
/* limitations in our own old affinity code, we used to limit */
/* Windows 32-bit to 32 workers, Windows 64-bit to 64 workers. */

#define MAX_NUM_WORKERS 1024		/* Number of launchable workers */

/* Factoring limits based on complex formulas given the speed of the */
/* factoring code vs. the speed of the Lucas-Lehmer code */
/* As an example, examine factoring to 2^68 (finding all 68-bit factors). */
/* First benchmark a machine to get LL iteration times and trial factoring */
/* times for a (16KB sieve of p=35000011). */
/*	We want to find when time spend eliminating an exponent with */
/* trial factoring equals time saved running 2 LL tests. */

/*	runs to find a factor (68) *
	#16KB sections (2^68-2^67)/p/(120/16)/(16*1024*8) *
	factoring_benchmark = 2.0 * LL test time (p * ll_benchmark)

	simplifying:

	68 * (2^68-2^67)/p/(120/16)/(16*1024*8) * facbench = 2 * p * llbench
	68 * 2^67 / p / (120/16) / 2^17 * facbench = 2 * p * lltime
	68 * 2^49 / p / (120/16) * facbench = p * lltime
	68 * 2^49 / (120/16) * facbench = p^2 * lltime
	68 * 2^53 / 120 * facbench = p^2 * lltime
	68 * 2^53 / 120 * facbench / lltime = p^2
	sqrt (68 * 2^53 / 120 * facbench / lltime) = p
*/

/* Now lets assume 30% of these factors would have been found by P-1.  So
   we only save a relatively quick P-1 test instead 2 LL tests.  Thus:
	sqrt (68 / 0.7 * 2^53 / 120 * facbench / lltime) = p
*/

/* Now factor in that 35000000 does 19 squarings, but 70000000 requires 20.
   Thus, if maxp is the maximum exponent that can be handled by an FFT size:
	sqrt (68 / 0.7 * 2^53 / 120 *
	      facbench * (1 + LOG2 (maxp/35000000) / 19) / lltime) = p
*/

/* Now factor in that errors sometimes force us to run more than 2 LL tests.
   Assume, 2.04 on average:
	sqrt (68 / 0.7 * 2^53 / 120 *
	      facbench * (1 + LOG2 (maxp/35000000) / 19) / lltime / 1.02) = p
*/

/* These breakeven points were calculated on a 2.5 GHz Core 2 using 64-bit prime95 v26.6: */
/* These should be recalculated for version 29 which now supports multithreaded TF and AVX-512 support */
/* However, GPUs make all these numbers somewhat obsolete. */

#define FAC82	1071000000L
#define FAC81	842000000L
#define FAC80	662000000L
#define FAC79	516800000L
#define FAC78	408400000L
#define FAC77	322100000L
#define FAC76	253500000L
#define FAC75	199500000L
#define FAC74	153400000L
#define FAC73	120000000L
#define FAC72	96830000L
#define FAC71	77910000L
#define FAC70	60940000L
#define FAC69	48800000L
#define FAC68	38300000L
#define FAC67	29690000L	/* We didn't bother calculating any smaller breakevens */

/* These breakeven points we're calculated on a 2.0 GHz P4 Northwood (using v24?): */

//#define FAC80	516000000L
//#define FAC79	420400000L
//#define FAC78	337400000L
//#define FAC77	264600000L
//#define FAC76	227300000L
//#define FAC75	186400000L
//#define FAC74	147500000L
//#define FAC73	115300000L
//#define FAC72	96830000L
//#define FAC71	75670000L
//#define FAC70	58520000L
//#define FAC69	47450000L
//#define FAC68	37800000L
//#define FAC67	29690000L
#define FAC66	23390000L

/* These breakevens we're calculated a long time ago on unknown hardware: */

#define FAC65	13380000L
#define FAC64	8250000L
#define FAC63	6515000L
#define FAC62	5160000L
#define FAC61	3960000L
#define FAC60	2950000L
#define FAC59	2360000L
#define FAC58	1930000L
#define FAC57	1480000L
#define FAC56	1000000L

/* Global variables */

extern int USE_V4;
extern char V4_USERID[15];
extern char V4_USERPWD[9];
extern char V4_USERNAME[80];

extern char INI_FILE[260];		/* Name of the prime INI file */
extern char WORKTODO_FILE[260];		/* Name of the work-to-do INI file */
extern char RESFILE[260];		/* Name of the results file */
extern char RESFILEBENCH[260];		/* Name of the results.bench file */
extern char SPOOL_FILE[260];		/* Name of the spool file */
extern char LOGFILE[260];		/* Name of the server log file */
extern int NO_GUI;			/* True if there is no graphical user interface */

extern char USERID[21];			/* User's ID */
extern char COMPID[21];			/* Computer name */
extern char COMPUTER_GUID[33];		/* Global unique computer ID */
extern int USE_PRIMENET;		/* TRUE if we're using PrimeNet */
extern int DIAL_UP;			/* TRUE if we're dialing into */
					/* PrimeNet server */
extern unsigned int NUM_WORKERS;	/* Number of workers to launch */
extern unsigned int WORK_PREFERENCE[MAX_NUM_WORKERS];
					/* Type of work (factoring, testing, etc.) to get from the server. */
extern unsigned int CORES_PER_TEST[MAX_NUM_WORKERS];
					/* Number of cores gwnum can use in computations */
extern int HYPERTHREAD_TF;		/* TRUE if trial factoring should use hyperthreads */
extern int HYPERTHREAD_LL;		/* TRUE if FFTs (LL, P-1, ECM, PRP) should use hyperthreads */
extern unsigned int DAYS_OF_WORK;	/* How much work to retrieve from */
					/* the primenet server */
extern int STRESS_TESTER;		/* 1 if stress testing */
extern int volatile ERRCHK;		/* 1 to turn on roundoff error checking */
extern unsigned int PRIORITY;		/* Desired priority level */
extern int MANUAL_COMM;			/* Set on if user explicitly starts */
					/* all communication with the server */
extern float volatile CPU_WORKER_DISK_SPACE; /* Disk space in GB each worker is allowed to use */
extern unsigned int volatile CPU_HOURS;	/* Hours per day program will run */
extern int CLASSIC_OUTPUT;		/* LL and PRP output to worker windows should use the pre-v28.5 classic style */
extern int OUTPUT_ROUNDOFF;		/* LL and PRP output to worker windows shound include the roundoff error */
extern unsigned long volatile ITER_OUTPUT;/* Iterations between outputs */
extern unsigned long volatile ITER_OUTPUT_RES;/* Iterations between results file outputs */
extern unsigned long volatile DISK_WRITE_TIME;
					/* Number of minutes between writing */
					/* intermediate results to disk */
extern unsigned long volatile JACOBI_TIME; /* Run a Jacobi test every N hours */
extern unsigned int MODEM_RETRY_TIME;	/* How often to try sending msgs */
					/* to primenet server whem modem off */
extern unsigned int NETWORK_RETRY_TIME;	/* How often to try sending msgs */
					/* to primenet server */
extern float DAYS_BETWEEN_CHECKINS;	/* Days between sending updated */
					/* completion dates to the server */
extern int NUM_BACKUP_FILES;		/* Between 1 and 3 backup files (or 99 for overwrite) */
extern int NUM_JACOBI_BACKUP_FILES;	/* Number of extra backup files (they've passed the Jacobi error check) */
extern int SILENT_VICTORY;		/* Quiet find of new Mersenne prime */
extern int SILENT_VICTORY_PRP;		/* Quiet find of new PRP */
extern int RUN_ON_BATTERY;		/* Run program even on battery power */
extern int BATTERY_PERCENT;		/* Pause if battery below this percent charged */
extern int DEFEAT_POWER_SAVE;		/* If possible, call OS to force computer to run at full speed */
extern int TRAY_ICON;			/* Display tiny tray icon */
extern int HIDE_ICON;			/* Display no icon */
extern int MERGE_WINDOWS;		/* Flags indicating which MDI */
					/* windows to merge together */
#define MERGE_MAIN_WINDOW	0x1	/* Merge main into first worker */
#define MERGE_COMM_WINDOW	0x2	/* Merge comm into first worker */
#define MERGE_WORKER_WINDOWS	0x4	/* Merge all workers into one window */
#define MERGE_MAINCOMM_WINDOWS	0x8	/* Merge main and comm windows */
#define MERGE_NO_PREFIX		0x20	/* Output thread prefix on each line flag */

extern double UNOFFICIAL_CPU_SPEED;	/* Last calculated CPU Speed in MHz.  We only */
					/* lower the official CPU_SPEED (the one reported to */
					/* the server) after several lower measurements */
extern unsigned int ROLLING_AVERAGE;	/* Ratio of this computer's speed */
					/* compared to the expected speed */
					/* for this CPU */
extern unsigned int PRECISION;		/* Number of decimal places to output*/
					/* in percent complete lines */
extern int RDTSC_TIMING;		/* True if RDTSC is used to time */
extern int TIMESTAMPING;		/* True is timestamps to be output */
extern int CUMULATIVE_TIMING;		/* True if outputting cumulative time */
extern int CUMULATIVE_ROUNDOFF;		/* True if outputting cumulative min and max roundoff error */
extern int SEQUENTIAL_WORK;		/* 1 (the  default from early 2000's to 2020) -- No work is priority work */
					/* 0 (the default from 1996 to early 2000's) -- LL/PRP tests needing TF and P-1 are priority work */
					/* -1 (the default from 2020 on) -- Only certification work is priority work */
extern int WELL_BEHAVED_WORK;		/* TRUE if undocumented feature "well behaved worktodo file" is on. */
					/* This reduces the number of times worktodo.ini is read and written. */
extern unsigned long INTERIM_FILES;	/* Create save file every N iters */  
extern unsigned long INTERIM_RESIDUES;	/* Print residue every N iterations */
extern unsigned long HYPERTHREADING_BACKOFF; /* Pause prime95 if iterations */
					/* get too slow. */
extern int THROTTLE_PCT;		/* Percent CPU time prog should run */

extern int STARTUP_IN_PROGRESS;		/* TRUE if startup dialogs are up */

extern int LAUNCH_TYPE;			/* Type of workers launched */
extern unsigned int WORKERS_ACTIVE;/* Num workers running */
extern int WORKERS_STOPPING;	/* TRUE iff workers stopping */

extern unsigned int WORKTODO_COUNT;	/* Count of valid work lines */

extern int GIMPS_QUIT;			/* TRUE if we just successfully quit the GIMPS project */

extern gwthread COMMUNICATION_THREAD;	/* Handle for comm thread.  Set when comm thread is active. */
extern gwthread UPLOAD_THREAD;		/* Handle for proof file upload thread */

extern gwevent AUTOBENCH_EVENT;		/* Event to wake up workers after an auto-benchmark */
extern gwevent PROOF_UPLOAD_EVENT;	/* Event to wake up proof uploader */

/* Topology variables and routines */

extern hwloc_topology_t hwloc_topology;	/* Hardware topology */
extern uint32_t CPU_TOTAL_L1_CACHE_SIZE;/* Sum of all the L1 caches in KB as determined by hwloc */
extern uint32_t CPU_TOTAL_L2_CACHE_SIZE;/* Sum of all the L2 caches in KB as determined by hwloc */
extern uint32_t CPU_TOTAL_L3_CACHE_SIZE;/* Sum of all the L3 caches in KB as determined by hwloc */
extern uint32_t CPU_TOTAL_L4_CACHE_SIZE;/* Sum of all the L4 caches in KB as determined by hwloc */
extern uint32_t CPU_NUM_L1_CACHES;	/* Number of L1 caches as determined by hwloc */
extern uint32_t CPU_NUM_L2_CACHES;	/* Number of L2 caches as determined by hwloc */
extern uint32_t CPU_NUM_L3_CACHES;	/* Number of L3 caches as determined by hwloc */
extern uint32_t CPU_NUM_L4_CACHES;	/* Number of L4 caches as determined by hwloc */
extern int	CPU_L2_CACHE_INCLUSIVE;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
extern int	CPU_L3_CACHE_INCLUSIVE;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
extern int	CPU_L4_CACHE_INCLUSIVE;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
extern int OS_CAN_SET_AFFINITY;		/* hwloc supports setting CPU affinity (known exception is Apple) */
void topology_print_children (hwloc_obj_t obj, int);

/* New in 30.7, HW_ globals to describe the underlying hardware.  Earlier versions of prime95 did not deal well with asymmetric cores/threads/caches. */

extern uint32_t HW_NUM_CORES;		/* Total number of cores (physical processors) available */
extern uint32_t HW_NUM_THREADS;		/* Total number of threads (logical processors) available */
extern uint32_t HW_NUM_COMPUTE_CORES;	/* Number of high-performance cores that a program like ours should use (i.e. not AlderLake efficiency cores) */
extern uint32_t HW_NUM_THREADING_NODES;	/* Total number of nodes where it should be beneficial to assign a worker's cores within the same node */
extern uint32_t HW_NUM_COMPUTE_THREADING_NODES;	/* Same as HW_NUM_THREADING_NODES but only counting nodes governing compute cores */
extern uint32_t HW_NUM_NUMA_NODES;	/* Total number of NUMA nodes in the computer */

struct hw_core_info {
	uint16_t num_threads;		/* Number of threads (logical processors) running on this physical processor */
	uint16_t ranking;		/* For now, only two values are supported for Alder Lake.  1=compute, 0=efficiency. */
};
extern struct hw_core_info *HW_CORES;	/* Information on every core */

/* INI section and keyword strings */

extern const char KEY_NumWorkers[];
extern const char KEY_QuitGIMPS[];

extern const char SEC_Internals[];
extern const char KEY_V30OptionsConverted[];
extern const char KEY_OldCpuSpeed[];
extern const char KEY_NewCpuSpeed[];
extern const char KEY_NewCpuSpeedCount[];
extern const char KEY_RollingAverage[];
extern const char KEY_RollingHash[];
extern const char KEY_RollingStartTime[];
extern const char KEY_RollingCompleteTime[];
extern const char KEY_CertDailyMBRemaining[];
extern const char KEY_CertDailyCPURemaining[];
extern const char KEY_CertDailyRemainingLastUpdate[];
extern const char KEY_Pid[];
extern const char KEY_SrvrUID[];
extern const char KEY_SrvrComputerName[];
extern const char KEY_SrvrP00[];
extern const char KEY_SrvrPO1[];
extern const char KEY_SrvrPO2[];
extern const char KEY_SrvrPO3[];
extern const char KEY_SrvrPO4[];
extern const char KEY_SrvrPO5[];
extern const char KEY_SrvrPO6[];
extern const char KEY_SrvrPO7[];
extern const char KEY_SrvrPO8[];
extern const char KEY_SrvrPO9[];
extern const char KEY_LastEndDatesSent[];
extern const char KEY_WGUID_version[];
extern const char KEY_CertErrorCount[];

extern const char SEC_PrimeNet[];
extern const char KEY_DialUp[];
extern const char KEY_ProxyHost[];
extern const char KEY_ProxyUser[];
extern const char KEY_ProxyPass[];
extern const char KEY_ProxyMask[];
extern const char KEY_Debug[];
extern const char KEY_UploadRateLimit[];
extern const char KEY_UploadStartTime[];
extern const char KEY_UploadEndTime[];
extern const char KEY_DownloadDailyLimit[];
extern const char KEY_DownloadRateLimit[];
extern const char KEY_ProofUploads[];
extern const char KEY_ProofHashLength[];
extern const char KEY_ProofPower[];
extern const char KEY_ProofPowerMult[];
extern const char KEY_UploadChunkSize[];

extern const char SEC_Windows[];
extern const char KEY_MergeWindows[];
extern const char KEY_TrayIcon[];
extern const char KEY_HideIcon[];
extern const char KEY_Left[];
extern const char KEY_Right[];
extern const char KEY_Top[];
extern const char KEY_Bottom[];
extern const char KEY_ExitOnX[];

/* Common routines */

void generate_application_string (char *);
void getCpuInfo (void);
void getCpuDescription (char *, int);

unsigned int countCommas (const char *);

int isPrime (unsigned long p);
int start_sieve (int thread_num, uint64_t start, void **returned_si);		// Default sieve eliminates numbers with factors < 64K
int start_sieve_with_limit (int thread_num, uint64_t start, uint32_t max_elimination_factor, void **returned_si);
uint64_t sieve (void *si);
void end_sieve (void *si);
uint64_t _intgcd (uint64_t, uint64_t);
uint64_t modinv (uint64_t, uint64_t);
int relatively_prime (uint64_t, uint64_t);

void sorted_add_unique (int *, int *, int);
int is_number_in_list (int, const char *);

unsigned int strToMinutes (const char *);
void minutesToStr (unsigned int, char *);
void write_memory_settings (unsigned int, unsigned int, unsigned int, unsigned int);
int read_memory_settings (unsigned int *, unsigned int *, unsigned int *, unsigned int *);

void nameAndReadIniFiles (int named_ini_files);
void initCommCode (void);
int readIniFiles (void);

void processTimedIniFile (const char *);

void incorporateIniAddFiles (void);
int incorporateWorkToDoAddFile (void);

void PTOGetAll (const char *ini_filename, const char *keyword, unsigned int *array,
		unsigned int def_val);
void PTOSetAll (const char *ini_filename, const char *keyword, const char *shadow_keyword,
		unsigned int *array, unsigned int new_val);
void PTOSetOne (const char *ini_filename, const char *keyword, const char *shadow_keyword,
		unsigned int *array, int tnum, unsigned int new_val);
int PTOIsGlobalOption (unsigned int *array);
int PTOHasOptionChanged (const char *shadow_keyword, unsigned int *array, int tnum);


#define MAIN_THREAD_NUM		-2
#define COMM_THREAD_NUM		-1
void create_window (int thread_num);
void destroy_window (int thread_num);
void TileViews (void);
void base_title (int, const char *);
void title (int, const char *);
#define	WORKING_ICON	0
#define	IDLE_ICON	1
void ChangeIcon (int, int);
void BlinkIcon (int, int);
EXTERNC void OutputBoth (int, const char *);
void OutputBothBench (int, const char *);
void OutputBothErrno (int);
void OutputStr (int, const char *);
void OutputStrNoTimeStamp (int, const char *);
void RealOutputStr (int, const char *);
void OutputSomewhere (int, const char *);
void LogMsg (const char *);
int OutOfMemory (int);

/* Structures and definitions dealing with the worktodo.ini file */

#define WORK_FACTOR		0
#define WORK_TEST		1
#define WORK_DBLCHK		2
#define WORK_ADVANCEDTEST	3
#define WORK_ECM		4
#define WORK_PMINUS1		5
#define WORK_PPLUS1		6
#define WORK_PFACTOR		7
#define WORK_PRP		10
#define WORK_CERT		11
#define WORK_NONE		100	/* Comment line in worktodo.ini */
#define WORK_DELETED		101	/* Deleted work_unit */

struct work_unit {		/* One line from the worktodo file */
	int	work_type;	/* Type of work to do */
	char	assignment_uid[33]; /* Primenet assignment ID */
	char	extension[9];	/* Optional save file extension */
	double	k;		/* K in k*b^n+c */
	unsigned long b;	/* B in k*b^n+c */
	unsigned long n;	/* N in k*b^n+c */
	signed long c;		/* C in k*b^n+c */
	unsigned long minimum_fftlen;/* Minimum FFT length to use.  Zero means default fftlen. */
	double	sieve_depth;	/* How far it has been trial factored */
	double	factor_to;	/* How far we should trial factor to */
	int	pminus1ed;	/* TRUE if has been P-1 factored */
	uint64_t B1;		/* ECM, P-1, P+1 - Stage 1 bound */
	uint64_t B2;		/* ECM, P-1, P+1 - Stage 2 bound */
	uint64_t B2_start;	/* P-1 - Stage 2 start */
	int	nth_run;	/* P+1 - 1 for start 2/7, 2 for start 6/5, 3+ for random start */
	unsigned int skip_curves; /* ECM - number of curves from gmp_ecm_file to skip over */
	unsigned int curves_to_do; /* ECM - curves to try */
	uint64_t curve;		/* ECM - Specific curve to test (debug tool) */
	double	tests_saved;	/* Pfactor - primality tests saved if a factor is found */
	unsigned int prp_base;	/* PRP base to use */	
	int	prp_residue_type; /* PRP residue to output -- see primenet.h */
	int	prp_dblchk;	/* True if this is a doublecheck of a previous PRP */
	int	cert_squarings; /* Number of squarings required for PRP proof certification */
	char	*gmp_ecm_file;	/* Save file from GMP-ECM to run stage 2 on */
	char	*known_factors;	/* ECM, P-1, P+1, PRP - list of known factors */
	char	*comment;	/* Comment line in worktodo.txt */
		/* Runtime variables */
	struct work_unit *next; /* Next in doubly-linked list */
	struct work_unit *prev; /* Previous in doubly-linked list */
	int	in_use_count;	/* Count of threads accessing this work unit */
	int	high_memory_usage;/* Set if we are using a lot of memory.  If user changes the available memory */
				/* settings, then we should stop and restart our computations. */
	char	stage[10];	/* Test stage (e.g. TF,P-1,LL) */
	double	pct_complete;	/* Percent complete (misnomer as value is between 0.0 and 1.0) */
	unsigned long fftlen;	/* FFT length in use */
	int	ra_failed;	/* Set when register assignment fails, tells us not to try registering it again. */
};
struct work_unit_array {	/* All the lines for one worker */
	struct work_unit *first; /* First work unit */
	struct work_unit *last;	/* Last work unit */
};

int readWorkToDoFile (void);
int writeWorkToDoFile (int);
#define SHORT_TERM_USE		0
#define LONG_TERM_USE		1
struct work_unit *getNextWorkToDoLine (int, struct work_unit *, int);
void decrementWorkUnitUseCount (struct work_unit *, int);
#define ADD_TO_FRONT		1
#define ADD_TO_LOGICAL_END	2		// Add after last non-blank line
#define ADD_TO_END		3
#define ADD_BEFORE_SPECIFIC	4		// Add before w->next
#define ADD_AFTER_SPECIFIC	5		// Add after w->next
int addWorkToDoLine (int, struct work_unit *, int);
int updateWorkToDoLine (int, struct work_unit *);
int deleteWorkToDoLine (int, struct work_unit *, int);
int isWorkUnitActive (struct work_unit *);
int addToWorkUnitArray (unsigned int, struct work_unit *, int);

void rolling_average_work_unit_complete (int, struct work_unit *);
void invalidateNextRollingAverageUpdate (void);

/* More miscellaneous routines */

double work_estimate (int thread_num, struct work_unit *);
unsigned int factorLimit (struct work_unit *);
void guess_pminus1_bounds (int, double, unsigned long, unsigned long, signed long, double, double, uint64_t *, uint64_t *, uint64_t *, double *);

void strupper (char *);
int isHex (const char *);
void tempFileName (struct work_unit *, char *);
int fileExists (const char *);
void DirPlusFilename (char *, const char *);

int read_array (int fd, char *buf, size_t len, uint32_t *sum);
int write_array (int fd, const char *buf, size_t len, uint32_t *sum);
int read_giant (int fd, giant g, uint32_t *sum);
int write_giant (int fd, giant g, uint32_t *sum);
int read_gwnum (int fd, gwhandle *gwdata, gwnum g, uint32_t *sum);
int write_gwnum (int fd, gwhandle *gwdata, gwnum g, uint32_t *sum);
int read_double (int fd, double *val, uint32_t *sum);
int write_double (int fd, double dbl, uint32_t *sum);
int read_int32 (int fd, int32_t *val, uint32_t *sum);
int write_int32 (int fd, int32_t val, uint32_t *sum);
int read_uint32 (int fd, uint32_t *val, uint32_t *sum);
int write_uint32 (int fd, uint32_t val, uint32_t *sum);
int read_int64 (int fd, int64_t *val, uint32_t *sum);
int write_int64 (int fd, int64_t val, uint32_t *sum);
int read_uint64 (int fd, uint64_t *val, uint32_t *sum);
int write_uint64 (int fd, uint64_t val, uint32_t *sum);
int read_magicnum (int fd, uint32_t magicnum);
int read_header (int fd, uint32_t *version, struct work_unit *w, uint32_t *sum);
int write_header (int fd, uint32_t magicnum, uint32_t version, struct work_unit *w);
int read_checksum (int fd, uint32_t *sum);
int write_checksum (int fd, uint32_t sum);
// Deprecated versions (int = int32, long = uint32, slong = int32)
int read_long (int fd, unsigned long *val, uint32_t *sum);
#define write_long(fd,val,sum)	write_uint32 (fd, (uint32_t) (val), sum)
int read_int (int fd, int *val, uint32_t *sum);
#define write_int(fd,val,sum)	write_int32 (fd, (int32_t) (val), sum)
int read_slong (int fd, long *val, uint32_t *sum);
#define write_slong(fd,val,sum)	write_int32 (fd, (int32_t) (val), sum)

void formatMsgForResultsFile (char *, struct work_unit *);
int writeResults (const char *);
int writeResultsBench (const char *);
int writeResultsJSON (const char *);
void JSONaddExponent (char *JSONbuf, struct work_unit *w);
void JSONaddExponentKnownFactors (char *JSONbuf, struct work_unit *w);
void JSONaddProgramTimestamp (char *JSONbuf);
void JSONaddUserComputerAID (char *JSONbuf, struct work_unit *w);

/* Routines called by common routines */

unsigned long physical_memory (void);
unsigned long GetSuggestedMemory (unsigned long nDesiredMemory);
int getDefaultTimeFormat (void);

/******************************************************************************
*                 Spool File and Server Communication Code                    *
******************************************************************************/

void init_spool_file_and_comm_code (void);
void set_comm_timers (void);
void clear_comm_rate_limits (void);
void do_manual_comm_now (void);
void pingServer (void);
void UpdateEndDates (void);
void ConditionallyUpdateEndDates (void);
#define MSG_CHECK_WORK_QUEUE		998
#define MSG_QUIT_GIMPS			999
void spoolMessage (short, void *);
void spoolExistingResultsFile (void);
int unreserve (unsigned long);
void salvageCorruptSpoolFile (void);
void proofUploader (void *);

int LoadPrimeNet (void);
void UnloadPrimeNet (void);
int PRIMENET (short, void *);
int ProofFileNames (char filenames[50][255]);
void ProofUpload (char *);
int ProofGetData (char *, void *, int, char *);
char getDirectorySeparator ();
void mallocFreeForOS ();


/******************************************************************************
*                           Timed Events Handler                              *
******************************************************************************/

#define TE_MEM_CHANGE		0	/* Night/day memory change event */
#define TE_PAUSE_WHILE		1	/* Check pause_while_running event */
#define TE_WORK_QUEUE_CHECK	2	/* Check for CERT work timer.  Also check regular work queue which also get checked for results sent, etc. */
#define TE_COMM_SERVER		3	/* Retry communication with server event */
#define TE_COMM_KILL		4	/* Kill hung communication thread event */
#define TE_PRIORITY_WORK	5	/* Check for priority work event */
#define TE_COMPLETION_DATES	6	/* Send expected completion dates event */
#define TE_THROTTLE		7	/* Sleep due to Throttle=n event */
#define TE_SAVE_FILES		8	/* Trigger the writing of save files */
#define TE_BATTERY_CHECK	9	/* Check battery status frequently */
#define TE_ROLLING_AVERAGE	10	/* Adjust rolling average */
#define TE_READ_PAUSE_DATA	11	/* Reread PauseWhileRunning info */
#define TE_READ_INI_FILE	12	/* Reread prime.txt settings because a during/else time period has ended */
#define TE_LOAD_AVERAGE		13	/* Linux/FreeBSD/Apple load average check */
#define TE_BENCH		14	/* Generate benchmark data for best FFT selection */
#define TE_JACOBI		15	/* Trigger a Jacobi error check */

#define MAX_TIMED_EVENTS	16	/* Maximum number of timed events */

void init_timed_event_handler (void);

void add_timed_event (
	int	event_number,		/* Which event to add */
	int	time_to_fire);		/* When to start event (seconds from now) */

void delete_timed_event (
	int	event_number);		/* Which event to delete */

int is_timed_event_active (
	int	event_number);		/* Which event to test */

time_t timed_event_fire_time (
	int	event_number);		/* Which event to get fire time of */

#define TE_PRIORITY_WORK_FREQ	 1*60*60 /* Check priority work every hour. */
#define TE_BATTERY_CHECK_FREQ	 15	/* Check battery every 15 sec. */
#define TE_THROTTLE_FREQ	 5	/* Throttle every 5 sec. */
#define TE_ROLLING_AVERAGE_FREQ	 12*60*60 /* Adjust rolling every 12 hr. */
#define TE_BENCH_FREQ		 21*60*60 /* Generate auto-benchmark data every 21 hrs. */

#ifdef __cplusplus
}
#endif

#endif
