/*----------------------------------------------------------------------
| Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
+---------------------------------------------------------------------*/

#ifndef _COMMONB_H
#define _COMMONB_H

/* This is used by C and C++ code.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

//#define SERVER_TESTING

/* These macros produce the security codes used for sending in results */
/* The security.h file is not required to compile a trial version of */
/* this program. */

#include "security.h"
#ifndef SEC1
#define SEC1(p)			0UL
#define SEC2(p,hi,lo,u,e)	0UL
#define SEC3(p)			0UL
#define SEC4(p)			0UL
#define SEC5(p,b1,b2)		0UL
#endif

/* Common routines */

#define ALL_WORKERS 8888	// Special value for first arg of LaunchWorkers
int LaunchWorkers (int, int);
int LaunchTortureTest (unsigned long, int);
int LaunchBench (int, int);
int LaunchAdvancedTime (unsigned long, unsigned long);

/* Callback routines */

#define LD_CONTINUE	0		/* Call primeContinue */
#define LD_TIME		1		/* Call primeTime */
#define LD_BENCH	2		/* Call primeBench */
#define LD_TORTURE	3		/* Call selfTest */
void PreLaunchCallback (int launch_type);	/* Implemented for each OS */
void PostLaunchCallback (int launch_type);	/* Implemented for each OS */
void stopCheckCallback (int thread_num);
void checkPauseListCallback (void);

/* Routines and structures to set thread priority.  Since we may have */
/* different rules doing normal work vs. benchmarking vs. torture testing, */
/* we send that info to the SetPriority routine to use as it sees fit. */

#define SET_PRIORITY_NORMAL_WORK	1
#define SET_PRIORITY_BENCHMARKING	2
#define SET_PRIORITY_TORTURE		3
#define SET_PRIORITY_TIME		4
#define SET_PRIORITY_QA			5
#define SET_PRIORITY_BUSY_LOOP		6
struct PriorityInfo {
 	uint32_t type;			/* Type defined above */
	uint32_t worker_num;		/* Worker number -- output informative messages to this worker's window */
	uint32_t verbosity;		/* Output affinity messages to the worker window */
	uint32_t aux_thread_num;	/* Set when gwnum launches auxiliary threads */
	bool	aux_hyperthread;	/* Set when gwnum launches auxiliary prefetching hyperthread */
	bool	aux_polymult;		/* Set when polymult launches auxiliary threads */
	union {
		struct {		/* Normal work info */
			int	normal_work_hyperthreading;	/* True if worker will use hyperthreading */
		};
		struct {		/* Torture test info */
			int	torture_num_workers;		/* Total number of torture test worker windows */
			int	torture_core_num;		/* CPU core to torture */
			int	torture_thread_num;		/* Hyperthread number (nth torture test for this core) */
			bool	torture_hyperthreading;		/* True if doing a hyperthreaded torture test */
		};
		struct {		/* Advanced/Time info */
			bool	time_hyperthreading;		/* True if doing a hyperthreaded timing */
		};
		struct {		/* Benchmark info */
			int	bench_base_core_num;		/* First CPU core to set affinity to */
			bool	bench_hyperthreading;		/* True if doing a hyperthreaded benchmark */
		};
		struct {		/* Busy loop info */
			int	busy_loop_core;			/* CPU core to keep busy */
		};
	};
};
void SetPriority (struct PriorityInfo *);
void SetAuxThreadPriority (int aux_thread_num, int action, void *data);
/* The hwloc library numbers cores from 0 to HW_NUM_CORES-1.  But we do not necessarily assign cores in that order. */
/* With the introduction of Alder Lake, we first assign compute/performance cores.  Then assign efficiency cores. */
/* This routine maps "prime95 core numbers" into "hwloc core numbers", returning the index into the HW_CORES array */
uint32_t get_ranked_core (uint32_t core_num);
/* Return the number of threads gwnum will need to use when running on several possibly hyperthreaded cores */
uint32_t get_ranked_num_threads (uint32_t base_core_num, uint32_t num_cores, bool hyperthreading);
/* Return the number of threads gwnum will need to use when a worker is running on several possibly hyperthreaded cores */
uint32_t get_worker_num_threads (uint32_t worker_num, bool hyperthreading);

/* Internal routines that do the real work */

void create_worker_windows (int num_threads);
void Launcher (void *arg);
void LauncherDispatch (void *arg);
int primeContinue (int);
void tortureTestDefaultSizes (int torture_type, int num_threads, int *minfft, int *maxfft);
int tortureTest (int, int);
int selfTest (int, struct PriorityInfo *, struct work_unit *);
int primeTime (int, unsigned long, unsigned long);
int primeBench (int, int);
int primeFactor (int, struct PriorityInfo *, struct work_unit *, unsigned int);
int prime (int, struct PriorityInfo *, struct work_unit *, int);
int prp (int, struct PriorityInfo *, struct work_unit *, int);
int cert (int, struct PriorityInfo *, struct work_unit *, int);
void autoBench (void);

/* Utility routines */

int isKnownMersennePrime (unsigned long);
void makestr (unsigned long, unsigned long, unsigned long, char *);

int exponent_near_fft_limit (gwhandle *gwdata);
void calc_output_frequencies (gwhandle *gwdata, double *output_frequency, double *output_title_frequency);
double trunc_percent (double percent);
int testSaveFilesFlag (int thread_num);
int SleepFive (int thread_num);

#define TIMER_NL	0x1
#define TIMER_CLR	0x2
#define TIMER_OPT_CLR	0x4
#define TIMER_MS	0x8

void clear_timers (double *timers, int num_timers);
void clear_timer (double *timers, int i);
void start_timer (double *timers, int i);
void start_timer_from_zero (double *timers, int i);
void end_timer (double *timers, int i);
void divide_timer (double *timers, int i, int j);
double timer_value (double *timers, int i);
void print_timer (double *timers, int i, char *buf, int flags);

/* Data structure used in reading save files and their backups as well as renaming bad save files. */

typedef struct read_save_file_state {
	int	thread_num;
	int	read_attempt;
	int	a_save_file_existed;
	int	a_non_bad_save_file_existed;
	int	num_original_bad_files;
	int	num_save_files_renamed;
	char	base_filename[80];
	char	current_filename[80];
} readSaveFileState;

void readSaveFileStateInit (readSaveFileState *state, int thread_num, char *filename);
int saveFileExists (readSaveFileState *state);
void saveFileBad (readSaveFileState *state);
extern const char ALLSAVEBAD_MSG[];

/* Data structure used in writing save files and their backups */

typedef struct write_save_file_state {
	char	base_filename[80];
	int	num_ordinary_save_files;
	int	num_special_save_files;		/* Example: Number of save files to keep that passed the Jacobi error check */
	uint64_t special;			/* Bit array for which ordinary save files are special */
} writeSaveFileState;

void uniquifySaveFile (int thread_num, char *filename);
void writeSaveFileStateInit (writeSaveFileState *state, char *filename, int num_special_save_files);
int openWriteSaveFile (writeSaveFileState *state);
void closeWriteSaveFile (writeSaveFileState *state, int fd);
void setWriteSaveFileSpecial (writeSaveFileState *state);
void deleteWriteSaveFile (writeSaveFileState *state, int fd);
void unlinkSaveFiles (writeSaveFileState *state);

/* Stop routines */

/* Reasons returned for why we stopped processing a work unit */
#define STOP_ESCAPE		1	/* User hit escape key.  Stopping all workers. */
#define STOP_OUT_OF_MEM		2	/* Fatal out-of-memory error */
#define STOP_FILE_IO_ERROR	3	/* Fatal file I/O error */
#define STOP_FATAL_ERROR	4	/* Other fatal error */
#define STOP_ABORT		5	/* Abort current work unit */
#define STOP_WORKER		6	/* This worker was stopped or was never started */
#define STOP_PAUSE		7	/* This worker must pause because a program in the PauseWhileRunning is running. */
#define STOP_RETRY_LATER	8	/* Abort work unit, but try again later */
#define STOP_WORK_UNIT_COMPLETE	50	/* Work unit is done! */
#define STOP_PRIORITY_WORK	51	/* Priority work, restart thread */
#define STOP_BATTERY		52	/* On battery - pause */
#define STOP_AUTOBENCH		53	/* Stop worker for a little while to run auto-benchmarks */
#define STOP_PREV_WORK_UNIT	54	/* Switch worker to previous work unit, PRP cofactor of a newly found ECM factor */
#define STOP_REREAD_INI		100	/* Reread prime.ini because a during/else time period has changed */
#define STOP_RESTART		101	/* Important INI option changed */
#define STOP_MEM_CHANGED	102	/* Day/night memory change */
#define STOP_NOT_ENOUGH_MEM	103	/* Not enough memory for P-1 stage 2 */

EXTERNC int stopCheck (int);
void stop_workers_for_escape (void);
void stop_workers_for_restart (void);
void stop_workers_for_add_files (void);
void stop_workers_for_reread_ini (void);
#define RESTART_ALL			0xFFFF
#define RESTART_WORK_AVAILABLE		0x0001
#define RESTART_USER_START		0x0002
#define RESTART_END_PAUSE		0x0004
#define RESTART_MEM_WAIT		0x0008
#define RESTART_BATTERY			0x0010
#define RESTART_LOADAVG			0x0020
void restart_waiting_workers (int);
void restart_one_waiting_worker (int, int);
void stop_worker_for_abort (int);

/* Routines dealing with day/night memory settings */

void mem_settings_have_changed (void);
unsigned long max_mem (int);
int avail_mem (int, unsigned long, unsigned long, unsigned int *);
#define MEM_USAGE_NOT_SET 0x1		/* The mem_in_use value is just a guess as the work unit for the thread has not started yet or is restarting. */
#define MEM_RESTARTING	0x2		/* The mem_in_use value will be changing soon as the thread is being restarted because it was using too much memory. */
#define MEM_WILL_BE_VARIABLE_USAGE 0x4	/* The current work unit will be a variable memory user.  We just don't know how much it will use yet. */
#define	MEM_VARIABLE_USAGE 0x8		/* The current work unit is using a lot of memory now and if needed could use less if restarted. */
#define MEM_WAITING 0x10		/* Set if thread is waiting for another thread to stop before returning from set_memory_usage */
int set_memory_usage (int, int, unsigned long);
void set_restart_if_max_memory_change (int thread_num);
void clear_restart_if_max_memory_change (int thread_num);

int avail_mem_not_sufficient (int thread_num, unsigned long min_memory, unsigned long desired_memory);

/* Handy macros to help in calling memory routines.  Macros tell us how many gwnums fit in given megabytes AND how many megabytes are used by a given */
/* number of gwnums.  There are two versions, one for gwnums that are allocated individually, the other for gwnums that are allocated more densely using */
/* gwarray routines.  We assume code and other data will add 1MB or 2MB to our working set. */

#define array_gwnum_size(g)			round_up_to_multiple_of((gwnum_datasize(g)+GW_HEADER_SIZE(g)),64)
#define cvt_mem_to_gwnums(g,m)			cvt_mem_to_gwnums_adj(g,m,0.0)
#define cvt_mem_to_gwnums_adj(g,m,a)		((uint64_t)(((double)((m)-1)*1048576.0-(double)gwmemused(g))/(double)array_gwnum_size(g)+(a)))
#define cvt_gwnums_to_mem(g,n)			((unsigned long)(((double)gwmemused(g)+(double)(n)*(double)gwnum_size(g))/1048576.0)+2)
#define cvt_mem_to_array_gwnums(g,m)		cvt_mem_to_array_gwnums_adj(g,m,0.0)
#define cvt_mem_to_array_gwnums_adj(g,m,a)	((uint64_t)(((double)((m)-1)*1048576.0-(double)gwmemused(g))/(double)array_gwnum_size(g)+(a)))
#define cvt_array_gwnums_to_mem(g,n)		((unsigned long)(((double)gwmemused(g)+(double)(n)*(double)array_gwnum_size(g))/1048576.0)+2)
#define cvt_mem_to_estimated_gwnums(m,k,b,n,c)	((uint64_t)(((double)((m)-1)*1048576.0-(double)gwmap_to_memused(k,b,n,c))/(double)gwmap_to_estimated_size(k,b,n,c)))

/* battery routines */

void run_on_battery_changed (void);
int OnBattery (void);			/* Implemented for each OS */
void test_battery (void);

/* priority work routines */

void check_for_priority_work (void);
void stop_worker_for_advanced_test (int);

/* starting/stopping individual workers routines */

void start_one_worker (int);
void stop_one_worker (int);
unsigned int active_workers_count (void);

/* "pause while running" routines */

extern int PAUSEABLE_WORKERS_RUNNING;	/* One or more workers are paused because another program is running */
extern char **PAUSE_WHILE_RUNNING;	/* An array of program names that, if running, prime95 should pause. */
extern int PAUSE_WHILE_RUNNING_FREQ;	/* How often prime95 should check the pause-while-running list */
void read_pause_info (void);
void checkPauseWhileRunning (void);
void implement_pause (int thread_num);
int is_LowMemWhileRunning_active (int thread_num);
void isInPauseList (char *program_name);

/* Load average routines */

void read_load_average_info (void);
double get_load_average (void);		/* Implemented by each OS */
void checkLoadAverage (void);
void implement_loadavg (int thread_num);

/* throttle routines */

void start_throttle_timer (void);
void stop_throttle_timer (void);
int handleThrottleTimerEvent (void);
void implementThrottle (int thread_num);

/* Routines called by common routines */

void clearThreadHandleArray (void);
void setOsThreadPriority (int);
void registerThreadTermination (void);
void raiseAllWorkersPriority (void);
void flashWindowAndBeep (void);
int primeSieveTest (int);
int test_randomly (int, struct PriorityInfo *);
int test_all_impl (int, struct PriorityInfo *);

/* Messages */

#define BENCH_SPEED  "The CPU speed in Options/CPU may not be correct.  An incorrect value will result in inaccurate timings.  Are you sure this is the correct CPU speed value?"

#ifdef __cplusplus
}
#endif

#endif
