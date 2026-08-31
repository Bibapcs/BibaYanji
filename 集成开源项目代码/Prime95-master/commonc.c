/*----------------------------------------------------------------------
| Copyright 1995-2024 Mersenne Research, Inc.  All rights reserved
|
| This file contains routines and global variables that are common for
| all operating systems the program has been ported to.  It is included
| in one of the source code files of each port.  See common.h for the
| common #defines and common routine definitions.
|
| Commona contains information used only during setup
| Commonb contains information used only during execution
| Commonc contains information used during setup and execution
+---------------------------------------------------------------------*/

static const char JUNK[]="Copyright 1996-2024 Mersenne Research, Inc. All rights reserved";

char	INI_FILE[260] = {0};
char	WORKTODO_FILE[260] = {0};
char	RESFILE[260] = {0};
char	RESFILEBENCH[260] = {0};
char	RESFILEJSON[260] = {0};
char	SPOOL_FILE[260] = {0};
char	LOGFILE[260] = {0};
char	SCREENLOGFILE[260] = {0};
char	*RESFILES[3] = {RESFILE, RESFILEBENCH, RESFILEJSON};
int	NO_GUI = 1;

char	USERID[21] = {0};
char	COMPID[21] = {0};
char	COMPUTER_GUID[33] = {0};
char	HARDWARE_GUID[33] = {0};
char	WINDOWS_GUID[33] = {0};
int	USE_PRIMENET = 0;
int	DIAL_UP = 0;
unsigned int DAYS_OF_WORK = 5;
int	STRESS_TESTER = 0;
int volatile ERRCHK = 0;
unsigned int PRIORITY = 1;
unsigned int NUM_WORKERS = 1;		/* Number of workers to launch */
unsigned int WORK_PREFERENCE[MAX_NUM_WORKERS] = {0};
unsigned int CORES_PER_TEST[MAX_NUM_WORKERS] = {1}; /* Number of cores gwnum can use in computations. */
int	HYPERTHREAD_TF = 1;		/* TRUE if trial factoring should use hyperthreads */
int	HYPERTHREAD_LL = 0;		/* TRUE if FFTs (LL, P-1, ECM, PRP) should use hyperthreads */
int	MANUAL_COMM = 0;
int	this_is_a_manual_comm = FALSE;	/* Ugly hack to let comm thread know it was triggered by a user's manual comm */
float volatile CPU_WORKER_DISK_SPACE = 6.0;
unsigned int volatile CPU_HOURS = 0;
int	CLASSIC_OUTPUT = 0;
int	OUTPUT_ROUNDOFF = 0;
unsigned long volatile ITER_OUTPUT = 0;
unsigned long volatile ITER_OUTPUT_RES = 999999999;
unsigned long volatile DISK_WRITE_TIME = 30;
unsigned long volatile JACOBI_TIME = 12; /* Run a Jacobi test every 12 hours */
unsigned int MODEM_RETRY_TIME = 2;
unsigned int NETWORK_RETRY_TIME = 70;
float	DAYS_BETWEEN_CHECKINS = 1.0;
int	NUM_BACKUP_FILES = 3;
int	NUM_JACOBI_BACKUP_FILES = 2;
int	SILENT_VICTORY = 0;
int	SILENT_VICTORY_PRP = 1;
int	RUN_ON_BATTERY = 1;
int	BATTERY_PERCENT = 0;
int	DEFEAT_POWER_SAVE = 1;
int	TRAY_ICON = TRUE;
int	HIDE_ICON = FALSE;
int	MERGE_WINDOWS = 0;		/* Flags indicating which MDI windows to merge together */
double UNOFFICIAL_CPU_SPEED = 0.0;
unsigned int ROLLING_AVERAGE = 0;
unsigned int PRECISION = 2;
int	RDTSC_TIMING = 1;
int	TIMESTAMPING = 1;
int	TIMESTAMPING_UTC = 0;
int	LOGTIMESTAMPING = 1;
int	LOGTIMESTAMPING_UTC = 0;
int	SCREENLOG = 0;
int	CUMULATIVE_TIMING = 0;
int	CUMULATIVE_ROUNDOFF = 1;
int	SEQUENTIAL_WORK = -1;
int	WELL_BEHAVED_WORK = 0;
unsigned long INTERIM_FILES = 0;
unsigned long INTERIM_RESIDUES = 0;
unsigned long HYPERTHREADING_BACKOFF = 0;
int	THROTTLE_PCT = 0;

int	STARTUP_IN_PROGRESS = 0;	/* True if displaying startup dialog boxes */

gwmutex	OUTPUT_MUTEX;			/* Lock for screen and results file access */
gwmutex	LOG_MUTEX;			/* Lock for prime.log access */
gwmutex	WORKTODO_MUTEX;			/* Lock for accessing worktodo structure */

gwevent AUTOBENCH_EVENT;		/* Event to wake up workers after an auto-benchmark */
gwevent PROOF_UPLOAD_EVENT;		/* Event to wake up proof uploader */

int	LAUNCH_TYPE = 0;		/* Type of workers launched */
unsigned int WORKERS_ACTIVE = 0;	/* Number of workers running */
int	WORKERS_STOPPING = 0;		/* TRUE iff workers are stopping */

struct work_unit_array WORK_UNITS[MAX_NUM_WORKERS] = {0}; /* An array of work units for each worker */
unsigned int WORKTODO_COUNT = 0;	/* Count of valid work lines */
unsigned int WORKTODO_IN_USE_COUNT = 0;	/* Count of work units in use */
int	WORKTODO_CHANGED = 0;		/* Flag indicating worktodo file needs writing */

hwloc_topology_t hwloc_topology;	/* Hardware topology */
uint32_t CPU_TOTAL_L1_CACHE_SIZE = 0;	/* Sum of all the L1 caches in KB as determined by hwloc */
uint32_t CPU_TOTAL_L2_CACHE_SIZE = 0;	/* Sum of all the L2 caches in KB as determined by hwloc */
uint32_t CPU_TOTAL_L3_CACHE_SIZE = 0;	/* Sum of all the L3 caches in KB as determined by hwloc */
uint32_t CPU_TOTAL_L4_CACHE_SIZE = 0;	/* Sum of all the L4 caches in KB as determined by hwloc */
uint32_t CPU_NUM_L1_CACHES = 0;		/* Number of L1 caches as determined by hwloc */
uint32_t CPU_NUM_L2_CACHES = 0;		/* Number of L2 caches as determined by hwloc */
uint32_t CPU_NUM_L3_CACHES = 0;		/* Number of L3 caches as determined by hwloc */
uint32_t CPU_NUM_L4_CACHES = 0;		/* Number of L4 caches as determined by hwloc */
int	CPU_L2_CACHE_INCLUSIVE = -1;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
int	CPU_L3_CACHE_INCLUSIVE = -1;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
int	CPU_L4_CACHE_INCLUSIVE = -1;	/* 1 if inclusive, 0 if exclusive, -1 if not known */
int	OS_CAN_SET_AFFINITY = 1;	/* hwloc supports setting CPU affinity (known exception is Apple) */

/* New in 30.7, HW_ globals to describe the underlying hardware.  Earlier versions of prime95 did not deal well with asymmetric cores/threads/caches. */

uint32_t HW_NUM_CORES;			/* Total number of cores (physical processors) available */
uint32_t HW_NUM_THREADS;		/* Total number of threads (logical processors) available */
uint32_t HW_NUM_COMPUTE_CORES;		/* Number of cores that a program like ours should use (i.e. not Alder Lake efficiency cores) */
uint32_t HW_NUM_THREADING_NODES;	/* Total number of nodes where it should be beneficial to assign a worker's cores within the same node */
uint32_t HW_NUM_COMPUTE_THREADING_NODES;/* Same as HW_NUM_THREADING_NODES but only counting nodes governing compute cores */
uint32_t HW_NUM_NUMA_NODES;		/* Total number of NUMA nodes in the computer */
struct hw_core_info *HW_CORES = NULL;	/* Information on every core */

/* INI section and settings strings */

const char KEY_NumWorkers[] = "NumWorkers";
const char KEY_QuitGIMPS[] = "QuitGIMPS";

const char SEC_Internals[] = "Internals";
const char KEY_V30OptionsConverted[] = "V30OptionsConverted";
const char KEY_OldCpuSpeed[] = "OldCpuSpeed";
const char KEY_NewCpuSpeed[] = "NewCpuSpeed";
const char KEY_NewCpuSpeedCount[] = "NewCpuSpeedCount";
const char KEY_RollingAverage[] = "RollingAverage";
const char KEY_RollingHash[] = "RollingHash";
const char KEY_RollingStartTime[] = "RollingStartTime";
const char KEY_RollingCompleteTime[] = "RollingCompleteTime";
const char KEY_CertDailyMBRemaining[] = "CertDailyMBRemaining";
const char KEY_CertDailyCPURemaining[] = "CertDailyCPURemaining";
const char KEY_CertDailyRemainingLastUpdate[] = "CertDailyRemainingLastUpdate";
const char KEY_Pid[] = "Pid";
const char KEY_SrvrUID[] = "SrvrUID";
const char KEY_SrvrComputerName[] = "SrvrComputerName";
const char KEY_SrvrP00[] = "SrvrP00";
const char KEY_SrvrPO1[] = "SrvrPO1";
const char KEY_SrvrPO2[] = "SrvrPO2";
const char KEY_SrvrPO3[] = "SrvrPO3";
const char KEY_SrvrPO4[] = "SrvrPO4";
const char KEY_SrvrPO5[] = "SrvrPO5";
const char KEY_SrvrPO6[] = "SrvrPO6";
const char KEY_SrvrPO7[] = "SrvrPO7";
const char KEY_SrvrPO8[] = "SrvrPO8";
const char KEY_SrvrPO9[] = "SrvrPO9";
const char KEY_LastEndDatesSent[] = "LastEndDatesSent";
const char KEY_WGUID_version[] = "WGUID_version";
const char KEY_CertErrorCount[] = "CertErrorCount";

const char SEC_PrimeNet[] = "PrimeNet";
const char KEY_DialUp[] = "DialUp";
const char KEY_ProxyHost[] = "ProxyHost";
const char KEY_ProxyUser[] = "ProxyUser";
const char KEY_ProxyPass[] = "ProxyPass";
const char KEY_ProxyMask[] = "ProxyMask";
const char KEY_Debug[] = "Debug";
const char KEY_UploadRateLimit[] = "UploadRateLimit";
const char KEY_UploadStartTime[] = "UploadStartTime";
const char KEY_UploadEndTime[] = "UploadEndTime";
const char KEY_DownloadDailyLimit[] = "DownloadDailyLimit";
const char KEY_DownloadRateLimit[] = "DownloadRateLimit";
const char KEY_ProofUploads[] = "ProofUploads";
const char KEY_ProofHashLength[] = "ProofHashLength";
const char KEY_ProofPower[] = "ProofPower";
const char KEY_ProofPowerMult[] = "ProofPowerMult";
const char KEY_UploadChunkSize[] = "UploadChunkSize";

const char SEC_Windows[] = "Windows";
const char KEY_MergeWindows[] = "MergeWindows";
const char KEY_TrayIcon[] = "TrayIcon";
const char KEY_HideIcon[] = "HideIcon";
const char KEY_Left[] = "Left";
const char KEY_Right[] = "Right";
const char KEY_Top[] = "Top";
const char KEY_Bottom[] = "Bottom";
const char KEY_ExitOnX[] = "ExitOnX";

/* Forward declarations */

int parseWorkToDoLine (char *line, struct work_unit *w);

/* Generate the application string.  This is sent to the server in a */
/* UC (Update Computer info) call.  It is also displayed in the Help/About dialog box. */

void generate_application_string (
	char	*app_string)
{
#ifdef SECURITY_MODULES_PRESENT
	sprintf (app_string, "%s,Prime95,v%s,build %s",
#else
	sprintf (app_string, "%s,Untrusted Prime95,v%s,build %s",
#endif
		 PORT == 1 ? "Windows" :
		 PORT == 2 ? "Linux" :
		 PORT == 4 ? "Windows64" :
		 PORT == 5 ? "WindowsService" :
		 PORT == 6 ? "FreeBSD" :
		 PORT == 7 ? "OS/2" :
		 PORT == 8 ? "Linux64" :
		 PORT == 9 ? "Mac OS X" :
		 PORT == 10 ? "Mac OS X 64-bit" :
		 PORT == 11 ? "Haiku" :
		 PORT == 12 ? "FreeBSD 64-bit" : "Unknown",
		 VERSION, BUILD_NUM);
}

/* Calculate the 32-byte hex string for the hardware GUID.  We use the */
/* output of the CPUID function to generate this.  We don't include the */
/* cache size information because when a new processor comes out the CPUID */
/* does not recognize the cache size.  When a new version of prime95 is */
/* released that does recognize the cache size a different hardware GUID */
/* would be generated. */

void calc_hardware_guid (void)
{
	char	buf[500];

	sprintf (buf, "%s%d", CPU_BRAND, CPU_SIGNATURE);
	md5_hexdigest_string (HARDWARE_GUID, buf);

/* Sometimes a user might want to run the program on several machines. */
/* Typically this is done by carrying the program and files around on a */
/* portable media such as a USB memory stick.  In this case, */
/* we need to defeat the code that automatically detects hardware changes. */
/* The FixedHardwareUID INI option tells us to get the Windows and */
/* hardware hashes from the INI file rather than calculating them. */

	if (IniGetInt (INI_FILE, "FixedHardwareUID", 0)) {
		IniGetString (INI_FILE, "HardwareGUID", HARDWARE_GUID, sizeof (HARDWARE_GUID), HARDWARE_GUID);
		IniWriteString (INI_FILE, "HardwareGUID", HARDWARE_GUID);
	}
}

/* Calculate the 32-byte hex string for the Windows-only GUID.  For */
/* non-Windows systems, we set WINDOWS_GUID to "". */
/* NOTE: In version 25.7 and earlier we used our first attempt at */
/* generating a unique ID.  In version 25.8 and later we use a more */
/* robust method of getting the serial number and SID.  We call the */
/* older code for all computer GUIDs that were generated by 25.7 */

void calc_windows_guid (void)
{
#ifdef _WINDOWS_
	int	algorithm_version;
	char	buf[500];

	algorithm_version = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_WGUID_version, 1);
	if (algorithm_version == 1) {
		getWindowsSerialNumber (buf);
		getWindowsSID (buf + strlen (buf));
	} else {
		getWindowsSerialNumber_2 (buf);
		getWindowsSID_2 (buf + strlen (buf));
	}
	md5_hexdigest_string (WINDOWS_GUID, buf);
#else
	WINDOWS_GUID[0] = 0;
#endif

/* Sometimes a user might want to run the program on several machines. */
/* Typically this is done by carrying the program and files around on a */
/* portable media such as a USB memory stick.  In this case, */
/* we need to defeat the code that automatically detects hardware changes. */
/* The FixedHardwareUID INI option tells us to get the Windows and */
/* hardware hashes from the INI file rather than calculating them. */
/* NOTE: In a dual boot situation where Linux has already written out */
/* an empty WindowsGUID, then this code will write out the non-empty */
/* WindowsGUID when run on a Windows machine.  The server must not */
/* generate a CPU_IDENTITY_MISMATCH error in this case. */

	if (IniGetInt (INI_FILE, "FixedHardwareUID", 0)) {
		IniGetString (INI_FILE, "WindowsGUID", WINDOWS_GUID, sizeof (WINDOWS_GUID), WINDOWS_GUID);
		IniWriteString (INI_FILE, "WindowsGUID", WINDOWS_GUID);
	}
}

/* Clear cached program options.  This is done when the server requests */
/* all program options to be resent.  This is a fail-safe that lets the */
/* client and server resync if the server detects an inconsistency (like */
/* getting an assignment for worker #2 with num_workers = 1 */

void clearCachedProgramOptions (void)
{
	int	tnum;
	char	section_name[32];

	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO1, NULL);
	for (tnum = 0; tnum < MAX_NUM_WORKERS; tnum++) {
		sprintf (section_name, "Worker #%d", tnum+1);
		IniSectionWriteString (INI_FILE, section_name, KEY_SrvrPO1, NULL);
	}
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO2, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO3, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO4, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO5, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO6, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO7, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO8, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrPO9, NULL);
}

/* Generate a globally unique ID for this computer.  All Primenet */
/* communications are based on this value. */

void generate_computer_guid (void)
{
	char	buf[500];
	time_t	current_time;

	time (&current_time);
	sprintf (buf, "%s%d%f%d", CPU_BRAND, CPU_SIGNATURE, CPU_SPEED, (int) current_time);
	md5_hexdigest_string (COMPUTER_GUID, buf);
	IniWriteString (INI_FILE, "ComputerGUID", COMPUTER_GUID);

/* Clear out local copies of what we think the server knows about this computer */
/* The server now knows nothing about this computer because of the newly generated computer ID */

	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrUID, NULL);
	IniSectionWriteString (INI_FILE, SEC_Internals, KEY_SrvrComputerName, NULL);
	clearCachedProgramOptions ();

/* Since we're generating a new computer GUID, we can use the latest, */
/* most robust version of calculating the Windows GUID. */

	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_WGUID_version, 2);
	calc_windows_guid ();
}

/* Determine the CPU speed either empirically or by user overrides. */
/* getCpuType must be called prior to calling this routine. */

void getCpuSpeed (void)
{
	int	temp, old_cpu_speed, report_new_cpu_speed;

/* Guess the CPU speed using the RDTSC instruction */

	guessCpuSpeed ();

/* Now let the user override the cpu speed from the prime.txt file */

	temp = IniGetInt (INI_FILE, "CpuSpeed", 99);
	if (temp != 99) CPU_SPEED = temp;

/* Make sure the cpu speed is reasonable */

	if (CPU_SPEED > 50000) CPU_SPEED = 50000;
	if (CPU_SPEED < 25) CPU_SPEED = 25;

/* Set the unofficial CPU speed.  The unofficial CPU speed is the */
/* last CPU speed measurement.  The official CPU speed is the one */
/* reported to the server. */

	UNOFFICIAL_CPU_SPEED = CPU_SPEED;

/* If CPU speed is much less than the official CPU speed, then set a new */
/* official CPU speed only after several slower measurements. */
/* The reason for this is that erroneously (due to one aberrant CPU speed */
/* calculation) reducing the speed we report to the server may result */
/* in erroneously unreserving exponents. */

	report_new_cpu_speed = FALSE;
	old_cpu_speed = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_OldCpuSpeed, 0);
	if (CPU_SPEED < (double) old_cpu_speed * 0.97) {
		if (IniSectionGetInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeedCount, 0) <= 5) {
			if (CPU_SPEED > (double) IniSectionGetInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, 0))
				IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, (int) (CPU_SPEED + 0.5));
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeedCount, IniSectionGetInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeedCount, 0) + 1);
			CPU_SPEED = old_cpu_speed;
		} else {
			if (CPU_SPEED < (double) IniSectionGetInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, 0))
				CPU_SPEED = (double) IniSectionGetInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, 0);
			report_new_cpu_speed = TRUE;
		}
	}

/* If CPU speed is close to last reported CPU speed, then use it. */
/* tell the server, recalculate new completion dates, and reset the */
/* rolling average.  Don't do this on the first run (before the Welcome */
/* dialog has been displayed). */

	else if (CPU_SPEED < (double) old_cpu_speed * 1.03) {
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeedCount, 0);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, 0);
	}

/* If CPU speed is much larger than the speed reported to the server, then */
/* use this new speed and tell the server. */

	else {
		report_new_cpu_speed = TRUE;
	}

/* Report a new CPU speed.  Remember the new CPU speed, tell the server, */
/* recalculate new completion dates, and reset the rolling average in */
/* such a way as to reduce the chance of spurious unreserves.  Don't */
/* do this on the first run (before the Welcome dialog has been displayed). */

	if (report_new_cpu_speed) {
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_OldCpuSpeed, (int) (CPU_SPEED + 0.5));
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeedCount, 0);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_NewCpuSpeed, 0);
		if (old_cpu_speed) {
			if (WORKTODO_COUNT) {
				ROLLING_AVERAGE = (int) (ROLLING_AVERAGE * old_cpu_speed / CPU_SPEED);
				if (ROLLING_AVERAGE < 1000) ROLLING_AVERAGE = 1000;
			}
			else
				ROLLING_AVERAGE = 1000;
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingAverage, ROLLING_AVERAGE);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingStartTime, 0);
			spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
			UpdateEndDates ();
		}
	}
}

/* Internal routine to calculate the number of "threading nodes" in a cpu set.  A "threading node" represents a group of cores that we prefer to */
/* assign to a single worker.  For example, there may well be a performance penalty if a worker's threads are on two different physical CPUS or */
/* access data from two different L3 caches. */

// NOTE:  We should beef up prime95's understanding of threading nodes.  There really should be a heirarchy of threading nodes.
// Consider an 8/8 Alder Lake CPU.  The 8 performance cores share form a threading node, the 8 efficiency cores form another threading
// node that is comprised of 2 subnodes -- there are two L2 caches serving 4 cores each.

uint32_t calc_threading_nodes_for_cpuset (hwloc_bitmap_t cpuset)
{
	int	num_cores;
	int	num_objs;
	int	num_threading_nodes = 1;

	num_cores = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_CORE);

	num_objs = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_PACKAGE);
	if (num_objs < num_cores && num_objs > num_threading_nodes) num_threading_nodes = num_objs;

	num_objs = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_NUMANODE);
	if (num_objs < num_cores && num_objs > num_threading_nodes) num_threading_nodes = num_objs;

	num_objs = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_L4CACHE);
	if (num_objs < num_cores && num_objs > num_threading_nodes) num_threading_nodes = num_objs;
	num_objs = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_L3CACHE);
	if (num_objs < num_cores && num_objs > num_threading_nodes) num_threading_nodes = num_objs;
	num_objs = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, cpuset, HWLOC_OBJ_L2CACHE);
	if (num_objs < num_cores && num_objs > num_threading_nodes) num_threading_nodes = num_objs;

	return (num_threading_nodes);
}

/* Set the CPU flags based on the CPUID instruction.  Also, the advanced */
/* user can override our guesses. */

void getCpuInfo (void)
{
	int	depth, i, temp;
	uint32_t core;

/* Get the CPU info using CPUID instruction */	

	guessCpuType ();

/* New in version 29!  Use hwloc info to determine HW_NUM_CORES and HW_NUM_THREADS.  Also get number of NUMA nodes */
/* which we may use later on to allocate memory from the proper NUMA node. */
/* We still allow overriding these settings using the INI file. */

	HW_NUM_CORES = hwloc_get_nbobjs_by_type (hwloc_topology, HWLOC_OBJ_CORE);
	if (HW_NUM_CORES < 1) HW_NUM_CORES = hwloc_get_nbobjs_by_type (hwloc_topology, HWLOC_OBJ_PU);
	if (HW_NUM_CORES < 1) HW_NUM_CORES = 1;			// Shouldn't happen
	HW_NUM_THREADS = hwloc_get_nbobjs_by_type (hwloc_topology, HWLOC_OBJ_PU);
	if (HW_NUM_THREADS < 1) HW_NUM_THREADS = 1;		// Shouldn't happen
	HW_NUM_NUMA_NODES = hwloc_get_nbobjs_by_type (hwloc_topology, HWLOC_OBJ_NUMANODE);
	if (HW_NUM_NUMA_NODES < 1) HW_NUM_NUMA_NODES = 1;

/* Allow overriding the hwloc generated values for number of physical processors and NUMA nodes. */

	HW_NUM_CORES = IniGetInt (INI_FILE, "NumCPUs", HW_NUM_CORES);
	HW_NUM_CORES = IniGetInt (INI_FILE, "NumCores", HW_NUM_CORES);
	if (HW_NUM_CORES < 1) HW_NUM_CORES = 1;
	HW_NUM_THREADS = IniGetInt (INI_FILE, "NumThreads", HW_NUM_THREADS);
	if (HW_NUM_THREADS < 1) HW_NUM_THREADS = 1;
	HW_NUM_NUMA_NODES = IniGetInt (INI_FILE, "NumNUMANodes", HW_NUM_NUMA_NODES);
	if (HW_NUM_NUMA_NODES == 0) HW_NUM_NUMA_NODES = 1;

/* Create a structure to describe each physical core.  Initialize as performance cores with the appropriate number of hyperthreads. */

	free (HW_CORES);
	HW_CORES = (struct hw_core_info *) malloc (HW_NUM_CORES * sizeof (struct hw_core_info));
	for (core = 0; core < HW_NUM_CORES; core++) {
		HW_CORES[core].ranking = 1;		// Mark as a performance core
		// Calculate number of threads
		int	num_threads = 1;		// Default to one thread (no hyperthreading)
		hwloc_obj_t obj = hwloc_get_obj_by_type (hwloc_topology, HWLOC_OBJ_CORE, core);		/* Get core obj */
		if (obj == NULL) obj = hwloc_get_obj_by_type (hwloc_topology, HWLOC_OBJ_PU, core);	/* The above failed for someone use plan B */
		if (obj != NULL) num_threads = hwloc_get_nbobjs_inside_cpuset_by_type (hwloc_topology, obj->cpuset, HWLOC_OBJ_PU);
		// Let user override calculated number of threads
		char key[50];
		sprintf (key, "Core%" PRIu32 "NumThreads", core);
		num_threads = IniGetInt (INI_FILE, key, num_threads);
		if (num_threads < 1) num_threads = 1;
		HW_CORES[core].num_threads = (uint16_t) num_threads;
	}

/* New in version 29.5, get L1/L2/L3/L4 total cache size for use in determining torture test FFT sizes. */
/* Overwrite cpuid's linesize and associativity with hwloc's */

	CPU_TOTAL_L1_CACHE_SIZE = CPU_NUM_L1_CACHES = 0;
	CPU_TOTAL_L2_CACHE_SIZE = CPU_NUM_L2_CACHES = 0;
	CPU_TOTAL_L3_CACHE_SIZE = CPU_NUM_L3_CACHES = 0;
	CPU_TOTAL_L4_CACHE_SIZE = CPU_NUM_L4_CACHES = 0;
	CPU_L2_CACHE_INCLUSIVE = -1;
	CPU_L3_CACHE_INCLUSIVE = -1;
	CPU_L4_CACHE_INCLUSIVE = -1;
	for (depth = 0; depth < hwloc_topology_get_depth (hwloc_topology); depth++) {
		for (i = 0; i < (int) hwloc_get_nbobjs_by_depth (hwloc_topology, depth); i++) {
			hwloc_obj_t obj;
			const char *inclusive;

			obj = hwloc_get_obj_by_depth (hwloc_topology, depth, i);
			if (obj == NULL || obj->attr == NULL) break; // can't happen
			if (obj->type == HWLOC_OBJ_L1CACHE) {
				CPU_TOTAL_L1_CACHE_SIZE += (uint32_t) (obj->attr->cache.size >> 10);
				CPU_NUM_L1_CACHES++;
				if (obj->attr->cache.linesize > 0) CPU_L1_CACHE_LINE_SIZE = obj->attr->cache.linesize;
				if (obj->attr->cache.associativity > 0) CPU_L1_SET_ASSOCIATIVE = obj->attr->cache.associativity;
			}
			else if (obj->type == HWLOC_OBJ_L2CACHE) {
				CPU_TOTAL_L2_CACHE_SIZE += (uint32_t) (obj->attr->cache.size >> 10);
				CPU_NUM_L2_CACHES++;
				if (obj->attr->cache.linesize > 0) CPU_L2_CACHE_LINE_SIZE = obj->attr->cache.linesize;
				if (obj->attr->cache.associativity > 0) CPU_L2_SET_ASSOCIATIVE = obj->attr->cache.associativity;
				inclusive = hwloc_obj_get_info_by_name (obj, "Inclusive");
				if (inclusive != NULL) CPU_L2_CACHE_INCLUSIVE = atoi (inclusive);
			}
			else if (obj->type == HWLOC_OBJ_L3CACHE) {
				CPU_TOTAL_L3_CACHE_SIZE += (uint32_t) (obj->attr->cache.size >> 10);
				CPU_NUM_L3_CACHES++;
				if (obj->attr->cache.linesize > 0) CPU_L3_CACHE_LINE_SIZE = obj->attr->cache.linesize;
				if (obj->attr->cache.associativity > 0) CPU_L3_SET_ASSOCIATIVE = obj->attr->cache.associativity;
				inclusive = hwloc_obj_get_info_by_name (obj, "Inclusive");
				if (inclusive != NULL) CPU_L3_CACHE_INCLUSIVE = atoi (inclusive);
			}
			else if (obj->type == HWLOC_OBJ_L4CACHE) {
				CPU_TOTAL_L4_CACHE_SIZE += (uint32_t) (obj->attr->cache.size >> 10);
				CPU_NUM_L4_CACHES++;
				inclusive = hwloc_obj_get_info_by_name (obj, "Inclusive");
				if (inclusive != NULL) CPU_L4_CACHE_INCLUSIVE = atoi (inclusive);
			}
		}
	}

/* Overwrite the cache info calculated via CPUID as hwloc's info is more detailed and I believe more reliable. */
/* We are transitioning away from using the cache size global variables computed by the CPUID code. */

	if (CPU_NUM_L1_CACHES) CPU_L1_CACHE_SIZE = CPU_TOTAL_L1_CACHE_SIZE / CPU_NUM_L1_CACHES;
	if (CPU_NUM_L2_CACHES) CPU_L2_CACHE_SIZE = CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES;
	if (CPU_NUM_L3_CACHES) CPU_L3_CACHE_SIZE = CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES;

/* If hwloc could not figure out the cache sizes, use the cache sizes as determined by CPUID. */
/* Note that the CPUID code in gwnum is not good at determining the number of L2 and L3 caches. */
/* Fortunately, it should be rare that we rely on the CPUID code. */

	if (CPU_NUM_L1_CACHES == 0 && CPU_L1_CACHE_SIZE > 0) CPU_TOTAL_L1_CACHE_SIZE = CPU_L1_CACHE_SIZE * HW_NUM_CORES, CPU_NUM_L1_CACHES = HW_NUM_CORES;
	if (CPU_NUM_L2_CACHES == 0 && CPU_L2_CACHE_SIZE > 0) CPU_TOTAL_L2_CACHE_SIZE = CPU_L2_CACHE_SIZE, CPU_NUM_L2_CACHES = 1;
	if (CPU_NUM_L3_CACHES == 0 && CPU_L3_CACHE_SIZE > 0) CPU_TOTAL_L3_CACHE_SIZE = CPU_L3_CACHE_SIZE, CPU_NUM_L3_CACHES = 1;

/* New in version 30.7.  Get the number of CPU kinds, figure out which are the efficiency cores. */

	int kind_verbosity = IniGetInt (INI_FILE, "KindVerbosity", 0);
	int cpu_kinds = hwloc_cpukinds_get_nr (hwloc_topology, 0);
	if (kind_verbosity) {
		char	buf[200];
		sprintf (buf, "Hwloc %d kinds\n", cpu_kinds);
		writeResultsBench (buf);
	}

	HW_NUM_THREADING_NODES = 0;
	HW_NUM_COMPUTE_THREADING_NODES = 0;
	hwloc_bitmap_t cpuset = hwloc_bitmap_alloc ();
	for (int i = 0; i < cpu_kinds; i++) {
		int	efficiency;
		unsigned num_infos;
		struct hwloc_info_s *infos;
		uint32_t num_threading_nodes;

		// Get info on this cpu kind as well as which cpu cores are of this cpu kind
		if (hwloc_cpukinds_get_info (hwloc_topology, i, cpuset, &efficiency, &num_infos, &infos, 0) < 0) continue;
		if (kind_verbosity) {
			char	buf[200], str[80];
			hwloc_bitmap_snprintf (str, sizeof (str), cpuset);
			sprintf (buf, "Hwloc kind %d: Efficiency: %d, Cpuset: %s\n", i, efficiency, str);
			writeResultsBench (buf);
		}

		// Try and compute the number of "threading nodes" for this cpu kind.
		num_threading_nodes = calc_threading_nodes_for_cpuset (cpuset);
		HW_NUM_THREADING_NODES += num_threading_nodes;

		// Efficiency values are not set (-1), (0) most power efficient, or (1+) performance
		// We earlier marked all cores as performance cores.  We only need to change things if there are multiple cpu_kinds
		// and we encounter efficiency == 0.
		if (cpu_kinds == 1 || efficiency != 0) {
			HW_NUM_COMPUTE_THREADING_NODES += num_threading_nodes;
			continue;
		}

		// Look at all HW_CORE structures to see which need changing
		for (core = 0; core < HW_NUM_CORES; core++) {
			hwloc_obj_t obj;
			obj = hwloc_get_obj_by_type (hwloc_topology, HWLOC_OBJ_CORE, core);			/* Get proper core */
			if (obj == NULL) obj = hwloc_get_obj_by_type (hwloc_topology, HWLOC_OBJ_PU, core);	/* Get proper core */
			if (obj != NULL && hwloc_bitmap_isincluded (obj->cpuset, cpuset))
				HW_CORES[core].ranking = 0;							// Mark this core as an efficiency core
		}
	}
	hwloc_bitmap_free (cpuset);

	// Let user override efficiency rankings
	for (core = 0; core < HW_NUM_CORES; core++) {
		char key[50];
		sprintf (key, "Core%" PRIu32 "Ranking", core);
		HW_CORES[core].ranking = (uint16_t) IniGetInt (INI_FILE, key, HW_CORES[core].ranking);
	}

/* Calculate the number of compute cores (as opposed to efficiency cores) */

	HW_NUM_COMPUTE_CORES = 0;
	for (core = 0; core < HW_NUM_CORES; core++) if (HW_CORES[core].ranking >= 1) HW_NUM_COMPUTE_CORES++;
	ASSERTG (HW_NUM_COMPUTE_CORES > 0);

/* Calculate hardware GUID (global unique identifier) using the CPUID info. */
/* Well, it isn't unique but it is about as good as we can do and still have */
/* portable code.  Do this calculation before user overrides values */
/* derived from CPUID results. */

	calc_hardware_guid ();

/* Let the user override the cpu flags from the prime.txt file */

	temp = IniGetInt (INI_FILE, "CpuSupportsRDTSC", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_RDTSC;
	if (temp == 1) CPU_FLAGS |= CPU_RDTSC;
	temp = IniGetInt (INI_FILE, "CpuSupportsCMOV", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_CMOV;
	if (temp == 1) CPU_FLAGS |= CPU_CMOV;
	temp = IniGetInt (INI_FILE, "CpuSupportsPrefetch", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_PREFETCH;
	if (temp == 1) CPU_FLAGS |= CPU_PREFETCH;
	temp = IniGetInt (INI_FILE, "CpuSupportsPrefetchw", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_PREFETCHW;
	if (temp == 1) CPU_FLAGS |= CPU_PREFETCHW;
	temp = IniGetInt (INI_FILE, "CpuSupportsPrefetchwt1", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_PREFETCHWT1;
	if (temp == 1) CPU_FLAGS |= CPU_PREFETCHWT1;
	temp = IniGetInt (INI_FILE, "CpuSupportsSSE", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_SSE;
	if (temp == 1) CPU_FLAGS |= CPU_SSE;
	temp = IniGetInt (INI_FILE, "CpuSupportsSSE2", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_SSE2;
	if (temp == 1) CPU_FLAGS |= CPU_SSE2;
	temp = IniGetInt (INI_FILE, "CpuSupportsSSE4", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_SSE41;
	if (temp == 1) CPU_FLAGS |= CPU_SSE41;
	temp = IniGetInt (INI_FILE, "CpuSupports3DNow", 99);
	if (temp == 0) CPU_FLAGS &= ~(CPU_3DNOW + CPU_3DNOW_PREFETCH);
	if (temp == 1) CPU_FLAGS |= (CPU_3DNOW + CPU_3DNOW_PREFETCH);
	temp = IniGetInt (INI_FILE, "CpuSupportsAVX", 99);
	if (temp == 0) CPU_FLAGS &= ~(CPU_AVX | CPU_FMA3);
	if (temp == 1) CPU_FLAGS |= CPU_AVX;
	temp = IniGetInt (INI_FILE, "CpuSupportsFMA3", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_FMA3;
	if (temp == 1) CPU_FLAGS |= (CPU_AVX | CPU_FMA3);
	temp = IniGetInt (INI_FILE, "CpuSupportsFMA4", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_FMA4;
	if (temp == 1) CPU_FLAGS |= CPU_FMA4;
	temp = IniGetInt (INI_FILE, "CpuSupportsAVX2", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_AVX2;
	if (temp == 1) CPU_FLAGS |= CPU_AVX2;
	temp = IniGetInt (INI_FILE, "CpuSupportsAVX512F", 99);
	if (temp == 0) CPU_FLAGS &= ~CPU_AVX512F;
	if (temp == 1) CPU_FLAGS |= CPU_AVX512F;

/* Let the user override the L1/L2/L3/L4 cache size in prime.txt file */

	CPU_TOTAL_L1_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL1TotalCacheSize", CPU_TOTAL_L1_CACHE_SIZE);
	CPU_NUM_L1_CACHES = IniGetInt (INI_FILE, "CpuL1NumCaches", CPU_NUM_L1_CACHES);

	CPU_TOTAL_L2_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL2TotalCacheSize", CPU_TOTAL_L2_CACHE_SIZE);
	CPU_NUM_L2_CACHES = IniGetInt (INI_FILE, "CpuL2NumCaches", CPU_NUM_L2_CACHES);
	CPU_L2_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL2CacheSize", CPU_L2_CACHE_SIZE);
	CPU_L2_CACHE_LINE_SIZE = IniGetInt (INI_FILE, "CpuL2CacheLineSize", CPU_L2_CACHE_LINE_SIZE);
	CPU_L2_SET_ASSOCIATIVE = IniGetInt (INI_FILE, "CpuL2SetAssociative", CPU_L2_SET_ASSOCIATIVE);
	CPU_L2_CACHE_INCLUSIVE = IniGetInt (INI_FILE, "CpuL2CacheInclusive", CPU_L2_CACHE_INCLUSIVE);

	CPU_TOTAL_L3_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL3TotalCacheSize", CPU_TOTAL_L3_CACHE_SIZE);
	CPU_NUM_L3_CACHES = IniGetInt (INI_FILE, "CpuL3NumCaches", CPU_NUM_L3_CACHES);
	CPU_L3_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL3CacheSize", CPU_L3_CACHE_SIZE);
	CPU_L3_CACHE_LINE_SIZE = IniGetInt (INI_FILE, "CpuL3CacheLineSize", CPU_L3_CACHE_LINE_SIZE);
	CPU_L3_SET_ASSOCIATIVE = IniGetInt (INI_FILE, "CpuL3SetAssociative", CPU_L3_SET_ASSOCIATIVE);
	CPU_L3_CACHE_INCLUSIVE = IniGetInt (INI_FILE, "CpuL3CacheInclusive", CPU_L3_CACHE_INCLUSIVE);

	CPU_TOTAL_L4_CACHE_SIZE = IniGetInt (INI_FILE, "CpuL4TotalCacheSize", CPU_TOTAL_L4_CACHE_SIZE);
	CPU_NUM_L4_CACHES = IniGetInt (INI_FILE, "CpuL4NumCaches", CPU_NUM_L4_CACHES);
	CPU_L4_CACHE_INCLUSIVE = IniGetInt (INI_FILE, "CpuL4CacheInclusive", CPU_L4_CACHE_INCLUSIVE);

/* Let the user override the CPUID brand string.  It should never be necessary. */
/* However, one Athlon owner's brand string became corrupted with illegal characters. */

	IniGetString (INI_FILE, "CpuBrand", CPU_BRAND, sizeof(CPU_BRAND), CPU_BRAND);

/* Apply sane corrections if HW_NUM_COREs was reduced significantly) */

	if (CPU_NUM_L1_CACHES > HW_NUM_CORES) CPU_NUM_L1_CACHES = HW_NUM_CORES;
	if (CPU_NUM_L2_CACHES > HW_NUM_CORES) CPU_NUM_L2_CACHES = HW_NUM_CORES;
	if (CPU_NUM_L3_CACHES > HW_NUM_CORES) CPU_NUM_L3_CACHES = HW_NUM_CORES;
	if (CPU_NUM_L4_CACHES > HW_NUM_CORES) CPU_NUM_L4_CACHES = HW_NUM_CORES;
	if (HW_NUM_NUMA_NODES > HW_NUM_CORES) HW_NUM_NUMA_NODES = HW_NUM_CORES;

/* Let user override the CPU architecture */

	CPU_ARCHITECTURE = IniGetInt (INI_FILE, "CpuArchitecture", CPU_ARCHITECTURE);

/* Allow overriding the calculated number of threading nodes */

	HW_NUM_THREADING_NODES = IniGetInt (INI_FILE, "NumThreadingNodes", HW_NUM_THREADING_NODES);
	if (HW_NUM_THREADING_NODES < 1) HW_NUM_THREADING_NODES = 1;
	HW_NUM_COMPUTE_THREADING_NODES = IniGetInt (INI_FILE, "NumComputeThreadingNodes", HW_NUM_COMPUTE_THREADING_NODES);
	if (HW_NUM_COMPUTE_THREADING_NODES < 1) HW_NUM_COMPUTE_THREADING_NODES = 1;

/* Now get the CPU speed */

	getCpuSpeed ();
}

/* Format a long or very long textual cpu description */

void getCpuDescription (
	char	*buf,			/* A 512 byte buffer */
	int	long_desc)		/* True for a very long description */
{

/* Recalculate the CPU speed in case speed step has changed the original settings. */

	getCpuSpeed ();

/* Now format a pretty CPU description */

	sprintf (buf, "%s\nCPU speed: %.2f MHz", CPU_BRAND, UNOFFICIAL_CPU_SPEED);
	if (HW_NUM_CORES != HW_NUM_COMPUTE_CORES)
		sprintf (buf + strlen (buf), ", %" PRIu32 "/%" PRIu32 " performance/efficiency cores", HW_NUM_COMPUTE_CORES, HW_NUM_CORES - HW_NUM_COMPUTE_CORES);
	else if (HW_NUM_CORES > 1 && HW_NUM_CORES != HW_NUM_THREADS)
		sprintf (buf + strlen (buf), ", %" PRIu32 " hyperthreaded cores", HW_NUM_CORES);
	else if (HW_NUM_CORES > 1)
		sprintf (buf + strlen (buf), ", %" PRIu32 " cores", HW_NUM_CORES);
	else if (HW_NUM_CORES != HW_NUM_THREADS)
		sprintf (buf + strlen (buf), ", with hyperthreading");
	strcat (buf, "\n");
	if (CPU_FLAGS) {
		strcat (buf, "CPU features: ");
//		if (CPU_FLAGS & CPU_RDTSC) strcat (buf, "RDTSC, ");
//		if (CPU_FLAGS & CPU_CMOV) strcat (buf, "CMOV, ");
//		if (CPU_FLAGS & CPU_MMX) strcat (buf, "MMX, ");
		if (CPU_FLAGS & CPU_3DNOW) strcat (buf, "3DNow!, ");
		else if (CPU_FLAGS & CPU_3DNOW_PREFETCH) strcat (buf, "3DNow! Prefetch, ");
		else if (CPU_FLAGS & CPU_PREFETCHW) strcat (buf, "Prefetchw, ");
		else if (CPU_FLAGS & CPU_PREFETCH) strcat (buf, "Prefetch, ");
		if (CPU_FLAGS & CPU_SSE) strcat (buf, "SSE, ");
		if (CPU_FLAGS & CPU_SSE2) strcat (buf, "SSE2, ");
		if (CPU_FLAGS & CPU_SSE41) strcat (buf, "SSE4, ");
		if (CPU_FLAGS & CPU_AVX) strcat (buf, "AVX, ");
		if (CPU_FLAGS & CPU_AVX2) strcat (buf, "AVX2, ");
		if (CPU_FLAGS & (CPU_FMA3 | CPU_FMA4)) strcat (buf, "FMA, ");
		if (CPU_FLAGS & CPU_AVX512F) strcat (buf, "AVX512F, ");
		strcpy (buf + strlen (buf) - 2, "\n");
	}

	strcat (buf, "L1 cache size: ");
	if (CPU_NUM_L1_CACHES <= 0) strcat (buf, "unknown");
	if (CPU_NUM_L1_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L1_CACHES);
	if (CPU_NUM_L1_CACHES >= 1) sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L1_CACHE_SIZE / CPU_NUM_L1_CACHES);
	
	strcat (buf, ", L2 cache size: ");
	if (CPU_NUM_L2_CACHES <= 0) strcat (buf, "unknown");
	if (CPU_NUM_L2_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L2_CACHES);
	if (CPU_NUM_L2_CACHES >= 1) {
		if ((CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES) & 0x3FF)
			sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES);
		else
			sprintf (buf + strlen (buf), "%d MB", CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES / 1024);
	}

	if (CPU_NUM_L3_CACHES > 0) {
		strcat (buf, CPU_NUM_L4_CACHES > 0 ? "\n" : ", ");
		strcat (buf, "L3 cache size: ");
		if (CPU_NUM_L3_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L3_CACHES);
		if ((CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES) & 0x3FF)
			sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES);
		else
			sprintf (buf + strlen (buf), "%d MB", CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES / 1024);
	}

	if (CPU_NUM_L4_CACHES > 0) {
		strcat (buf, ", L4 cache size: ");
		if (CPU_NUM_L4_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L4_CACHES);
		if ((CPU_TOTAL_L4_CACHE_SIZE / CPU_NUM_L4_CACHES) & 0x3FF)
			sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L4_CACHE_SIZE / CPU_NUM_L4_CACHES);
		else
			sprintf (buf + strlen (buf), "%d MB", CPU_TOTAL_L4_CACHE_SIZE / CPU_NUM_L4_CACHES / 1024);
	}

	if (! long_desc) return;
	strcat (buf, "\nL1 cache line size: ");
	if (CPU_L1_CACHE_LINE_SIZE < 0) strcat (buf, "unknown");
	else sprintf (buf+strlen(buf), "%d bytes", CPU_L1_CACHE_LINE_SIZE);
	strcat (buf, ", L2 cache line size: ");
	if (CPU_L2_CACHE_LINE_SIZE < 0) strcat (buf, "unknown");
	else sprintf (buf+strlen(buf), "%d bytes", CPU_L2_CACHE_LINE_SIZE);
	strcat (buf, "\n");
}

/* Print the machine topology as discovered by hwloc library */

void topology_print_children (
	hwloc_obj_t obj,
        int depth)
{
	char type[32], attr[1024], cpuset[256], buf[1500];
	unsigned int i;
	if (obj == NULL) return;  // Shouldn't happen
	hwloc_obj_type_snprintf (type, sizeof(type), obj, 0);
	sprintf (buf, "%*s%s", 2*depth, " ", type);
	if (obj->os_index != (unsigned) -1)
		sprintf (buf+strlen(buf), "#%u", obj->os_index);
	hwloc_obj_attr_snprintf (attr, sizeof(attr), obj, ", ", 1 /* verbose */);
	if (obj->type == HWLOC_OBJ_CORE || obj->type == HWLOC_OBJ_PU)
		hwloc_bitmap_snprintf (cpuset, sizeof(cpuset), obj->cpuset);
	else
		cpuset[0] = 0;
	if (attr[0] && cpuset[0]) sprintf (buf+strlen(buf), " (%s, cpuset: %s)", attr, cpuset);
	else if (attr[0]) sprintf (buf+strlen(buf), " (%s)", attr);
	else if (cpuset[0]) sprintf (buf+strlen(buf), " (cpuset: %s)", cpuset);
	strcat (buf, "\n");
	writeResultsBench (buf);
	for (i = 0; i < obj->arity; i++) {
		topology_print_children (obj->children[i], depth + 1);
	}
}

/* Determine if a number is prime */

int isPrime (
	unsigned long p)
{
	unsigned long i;
	for (i = 2; i < 0xFFFF && i * i <= p; i = (i + 1) | 1)
		if (p % i == 0) return (FALSE);
	return (TRUE);
}


/* Routines that use a simple sieve to find "may be prime" numbers.  That is, numbers without any small factors. */
/* This is used by ECM, P-1, and P+1.  Also, used by 64-bit trial factoring setup code. */

typedef struct {
	uint32_t *primes;				// Sieving primes and the bit-to-clear
	uint64_t first_number;
	unsigned int bit_number;
	unsigned int num_primes;
	unsigned int num_elimination_primes;
	unsigned int max_elimination;
	uint64_t start;
	char	array[4096];
} sieve_info;

/* Internal routine to fill up the sieve array */

void fill_sieve (
	sieve_info *si)
{
	unsigned int i;
	uint32_t fmax;

/* Determine the first sieve bit to clear for each small prime */

	fmax = (uint32_t) sqrt ((double) (si->first_number + sizeof (si->array) * 8 * 2));
	for (i = si->num_primes; i < si->num_elimination_primes * 2; i += 2) {
		uint32_t f, r, bit;
		f = si->primes[i];
		if (f > fmax) break;
		if (si->first_number == 3) {
			bit = (f * f - 3) >> 1;
		} else {
			r = (uint32_t) (si->first_number % f);
			if (r == 0) bit = 0;
			else if (r & 1) bit = (f - r) / 2;
			else bit = (f + f - r) / 2;
			if (f == si->first_number + 2 * bit) bit += f;
		}
		si->primes[i+1] = bit;
	}
	si->num_primes = i;

/* Fill the sieve with ones, then zero out the composites */

	memset (si->array, 0xFF, sizeof (si->array));
	for (i = 0; i < si->num_primes; i += 2) {
		uint32_t f, bit;
		f = si->primes[i];
		for (bit = si->primes[i+1]; bit < sizeof (si->array) * 8; bit += f) bitclr (si->array, bit);
		si->primes[i+1] = bit - sizeof (si->array) * 8;
	}
	si->bit_number = 0;
}

/* Either:  1) Recycle a sieve_info structure using the same number of small primes, OR 2) Allocate a new sieve_info structure. */
/* In both cases, reset the sieve to start returning primes at the specified point. */

int start_sieve (
	int	thread_num,	 
	uint64_t start,
	void	**si_to_recycle_or_returned_new_si)	/* Recycled or returned sieving structure */
{
	// Default sieve eliminates numbers with factors < 64K
	return (start_sieve_with_limit (thread_num, start, 65536, si_to_recycle_or_returned_new_si));
}

int start_sieve_with_limit (
	int	thread_num,	 
	uint64_t start,					/* Starting point for the sieve */
	uint32_t max_elimination_factor,		/* Sieve eliminates composites with any factors less than this number */
	void	**si_to_recycle_or_returned_new_si)	/* Returned sieving structure */
{
	sieve_info *si;
	unsigned int i;

/* Re-use or allocate the sieve structure */

	if (*si_to_recycle_or_returned_new_si != NULL)
		si = (sieve_info *) *si_to_recycle_or_returned_new_si;
	else {
		si = (sieve_info *) malloc (sizeof (sieve_info));
		if (si == NULL) goto oom;
		*si_to_recycle_or_returned_new_si = si;
		memset (si, 0, sizeof (sieve_info));
	}

/* Remember starting point (in case its 2) and make real start odd */

	if (start < 2) start = 2;
	si->start = start;
	start |= 1;

/* Delete old small primes array if it is not big enough */

	if (si->primes != NULL && max_elimination_factor > si->max_elimination) {
		free (si->primes);
		si->primes = NULL;
		si->first_number = 0;
	}

/* See if we can just reuse the existing sieve */

	if (si->first_number && start >= si->first_number && start < si->first_number + sizeof (si->array) * 8 * 2) {
		si->bit_number = (unsigned int) (start - si->first_number) / 2;
		return (0);
	}

/* Initialize sieving primes */

	if (si->primes == NULL) {
		uint32_t f;
		unsigned int estimated_num_primes;
		estimated_num_primes = (unsigned int) ((double) max_elimination_factor / (log ((double) max_elimination_factor) - 1.0) * 1.01);
		si->primes = (uint32_t *) malloc (estimated_num_primes * 2 * sizeof (uint32_t));
		if (si->primes == NULL) goto oom;
		for (i = 0, f = 3; f <= max_elimination_factor && i < estimated_num_primes; f += 2)
			if (isPrime (f)) si->primes[i*2] = f, i++;
		si->num_elimination_primes = i;
		si->max_elimination = max_elimination_factor;
	}

	si->first_number = start;
	si->num_primes = 0;
	fill_sieve (si);
	return (0);

/* Out of memory exit path */

oom:	*si_to_recycle_or_returned_new_si = NULL;
	free (si);
	return (OutOfMemory (thread_num));
}

/* Return next prime from the sieve */

uint64_t sieve (
	void	*si_arg)
{
	sieve_info *si = (sieve_info *) si_arg;

	if (si->start == 2) {
		si->start = 3;
		return (2);
	}
	for ( ; ; ) {
		unsigned int bit;
		if (si->bit_number == sizeof (si->array) * 8) {
			si->first_number += 2 * sizeof (si->array) * 8;
			fill_sieve (si);
		}
		bit = si->bit_number++;
		if (bittst (si->array, bit))
			return (si->first_number + 2 * bit);
	}
}

/* Return next "may be prime" from the sieve */

void end_sieve (
	void	*si_arg)
{
	sieve_info *si = (sieve_info *) si_arg;
	if (si == NULL) return;
	free (si->primes);
	free (si);
}

/* Return the GCD of two numbers */

uint64_t _intgcd (
	uint64_t a,
	uint64_t b)
{
	while (b != 0) {
		uint64_t temp = a % b;
		a = b;
		b = temp;
	}
	return a;
}

/* Simple routine to determine if two numbers are relatively prime */

int relatively_prime (
	uint64_t a,
	uint64_t b)
{
	return (_intgcd (a, b) == 1);
}

/* Calculate the modular inverse - no error checking is performed */

uint64_t modinv (uint64_t x, uint64_t f)		/* Compute 1/x mod f */
{
	uint64_t u = x % f;
	uint64_t v = f;
	uint64_t tmp, q;
	int64_t c = 0, d = 1, stmp;

	while (u > 1) {
		q = u / v;
		tmp = v; v = u % v; u = tmp;
		stmp = c; c = d - q * c; d = stmp;
	}
	if (d < 0) d += f;
	return (d);
}

/* Add val to an array.  Horrible performance for big arrays. */

void sorted_add_unique (int *vals, int *numvals, int newval)
{
	int	i, j;

	for (i = 0; i < *numvals; i++) {
		if (vals[i] == newval) return;
		if (vals[i] < newval) continue;
		j = vals[i];
		vals[i] = newval;
		newval = j;
	}
	vals[*numvals] = newval;
	(*numvals)++;
}

/* Determine is number is included in a comma separated list of ranges (e.g. "1,4-6") */

int is_number_in_list (int val, const char *list)
{
	const char *p, *dash, *comma;
	int	start, end;

	for (p = list; ; p = comma+1) {		// Loop through comma-separated list
		start = atoi (p);
		dash = strchr (p, '-');
		comma = strchr (p, ',');
		if (dash != NULL && (comma == NULL || dash < comma)) end = atoi (dash+1);
		else end = start;
		if (val >= start && val <= end) return (TRUE);
		if (comma == NULL) break;
	}
	return (FALSE);
}

/* Upper case a string */

void strupper (
	char	*p)
{
	for ( ; *p; p++) if (*p >= 'a' && *p <= 'z') *p = *p - 'a' + 'A';
}

/* Return true if string contains all hex characters */

int isHex (
	const char *p)
{
	for ( ; *p; p++) if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f') && !(*p >= 'A' && *p <= 'F')) return (FALSE);
	return (TRUE);
}

/* Convert a string (e.g "11:30 AM") to minutes since midnight */
/* The result is from 0 to 1440 inclusive.  This allow specifying 00:00 - 24:00 to mean "all day" */

unsigned int strToMinutes (
	const char *buf)
{
	unsigned int hours, minutes, pm;

	pm = (strchr (buf, 'P') != NULL || strchr (buf, 'p') != NULL);
	hours = atoi (buf);
	while (isdigit (*buf)) buf++;
	while (*buf && ! isdigit (*buf)) buf++;
	minutes = atoi (buf);
	if (hours == 12) hours -= 12;
	if (pm) hours += 12;
	minutes = hours * 60 + minutes;
	if (minutes > 1440) minutes %= 1440;
	return (minutes);
}

/* Convert minutes since midnight to a string (e.g "11:30 AM") */

void minutesToStr (
	unsigned int minutes,
	char	*buf)
{
	unsigned int fmt_type, hours, pm;

	hours = minutes / 60;
	fmt_type = IniGetInt (INI_FILE, "AMPM", 0);
	if (fmt_type == 0) fmt_type = getDefaultTimeFormat ();
	if (fmt_type != 1) {
		sprintf (buf, "%d:%02d", hours, minutes % 60);
	} else {
		if (hours >= 12) hours -= 12, pm = 1;
		else pm = 0;
		if (hours == 0) hours = 12;
		sprintf (buf, "%d:%02d %s", hours, minutes % 60, pm ? "PM" : "AM");
	}
}

/* Convert the old DAY_MEMORY, NIGHT_MEMORY settings into the simpler */
/* and more powerful MEMORY setting. */

void write_memory_settings (
	unsigned int day_memory,
	unsigned int night_memory,
	unsigned int day_start_time,
	unsigned int day_end_time)
{
	char	buf[100];

	sprintf (buf, "%d during %d:%02d-%d:%02d else %d",
		 day_memory, day_start_time / 60, day_start_time % 60,
		 day_end_time / 60, day_end_time % 60, night_memory);
	IniWriteString (INI_FILE, "Memory", buf);
	IniWriteString (INI_FILE, "DayMemory", NULL);
	IniWriteString (INI_FILE, "NightMemory", NULL);
	IniWriteString (INI_FILE, "DayStartTime", NULL);
	IniWriteString (INI_FILE, "DayEndTime", NULL);
}

/* Convert the new MEMORY setting into the old DAY_MEMORY, NIGHT_MEMORY settings. */
/* Returns FALSE if the MEMORY setting cannot be converted. */

int read_memory_settings (
	unsigned int *day_memory,
	unsigned int *night_memory,
	unsigned int *day_start_time,
	unsigned int *day_end_time)
{
	const char *p, *during_clause, *else_clause;

/* Set up some default values */

	*day_memory = 256;
	*night_memory = 256;
	*day_start_time = 450;
	*day_end_time = 1410;

/* Get the memory settings.  If not found, return the defaults */

	p = IniSectionGetStringRaw (INI_FILE, NULL, "Memory");
	if (p == NULL) return (TRUE);

/* Find the time specifiers.  If none found, return the same value for */
/* day and night memory. */

	*day_memory = atol (p);
	during_clause = strstr (p, " during ");
	if (during_clause == NULL) {
		*night_memory = *day_memory;
		return (TRUE);
	}

/* Parse the time value to get the day start time and day end time */
/* It must be the exact same syntax as written by write_memory_settings */

	p = during_clause + 8;
	*day_start_time = atol (p) * 60;
	while (isdigit (*p)) p++;
	if (*p++ != ':') return (FALSE);
	*day_start_time += atol (p);
	while (isdigit (*p)) p++;
	if (*p++ != '-') return (FALSE);
	*day_end_time = atol (p) * 60;
	while (isdigit (*p)) p++;
	if (*p++ != ':') return (FALSE);
	*day_end_time += atol (p);
	while (isdigit (*p)) p++;

/* If user hand-edited the prime.txt file and entered an illegal time */
/* value, then arbitrarily set the value within the proper range */

	*day_start_time %= 1440;
	*day_end_time %= 1440;

/* Handle the else clause */

	else_clause = strstr (p, " else ");
	if (p != else_clause) return (FALSE);
	*night_memory = atol (p + 6);

	return (strstr (p, " during ") == NULL);
}

/* Callback routine when illegal line read from INI file. */

void ini_error_handler (
	const char *filename,
	int	line_number,
	const char *line_text)
{
	char	buf[1200];
	sprintf (buf, "File %s, line number %d is not valid: %s\n", filename, line_number, line_text);
	OutputSomewhere (MAIN_THREAD_NUM, buf);
}

/* Move an ini setting to its appropriate section */

void moveToSection (const char *section, const char *key)
{
	char val[256];
	// Hack to move a number of keys
	if (strstr (key, "%d") != NULL) {
		for (int i = 1; i < 10000; i++) {
			char	numbered_key[80];
			sprintf (numbered_key, key, i);
			IniGetString (INI_FILE, numbered_key, val, sizeof (val), NULL);
			if (val[0] == 0) return;
			IniSectionWriteString (INI_FILE, section, numbered_key, val);
			IniWriteString (INI_FILE, numbered_key, NULL);
		}
	}
	// Move a single key
	IniGetString (INI_FILE, key, val, sizeof (val), NULL);
	if (val[0]) {
		IniSectionWriteString (INI_FILE, section, key, val);
		IniWriteString (INI_FILE, key, NULL);
	}
}

/* Rename an ini setting */

void renameIniKey (const char *section, const char *old_key_name, const char *new_key_name)
{
	char val[256];
	IniSectionGetString (INI_FILE, section, old_key_name, val, sizeof (val), old_key_name);
	// If old key name does not exist, we are done
	if (strcmp (val, old_key_name) == 0) return;
	// Write to the new key name and delete the old key name
	IniSectionWriteString (INI_FILE, section, new_key_name, val);
	IniSectionWriteString (INI_FILE, section, old_key_name, NULL);
}

/* Determine the names of the INI files, then read them.  This is also the */
/* perfect time to initialize mutexes and do other initializations. */

void nameAndReadIniFiles (
	int	named_ini_files)
{
	char	localini_file[260];
	char	buf[513];

/* Determine the hardware topology using the hwloc library.  This library is much more */
/* advanced than the information we previously garnered from CPUID instructions and thread timings. */

	hwloc_topology_init (&hwloc_topology);
	hwloc_topology_load (hwloc_topology);

/* See if setting CPU affinity is supported */

	{
		const struct hwloc_topology_support *support;
		OS_CAN_SET_AFFINITY = 1;
		support = hwloc_topology_get_support (hwloc_topology);
		if (support == NULL || ! support->cpubind->set_thread_cpubind) OS_CAN_SET_AFFINITY = 0;
	}

/* Initialize mutexes */

	gwmutex_init (&OUTPUT_MUTEX);
	gwmutex_init (&LOG_MUTEX);
	gwmutex_init (&WORKTODO_MUTEX);

/* Figure out the names of the INI files */

	if (named_ini_files < 0) {
		strcpy (INI_FILE, "prime.txt");
		strcpy (localini_file, "local.txt");
		strcpy (WORKTODO_FILE, "worktodo.txt");
		strcpy (RESFILE, "results.txt");
		strcpy (RESFILEBENCH, "results.bench.txt");
		strcpy (RESFILEJSON, "results.json.txt");
		strcpy (SPOOL_FILE, "prime.spl");
		strcpy (LOGFILE, "prime.log");
		strcpy (SCREENLOGFILE, "screen.log");
	} else {
		sprintf (INI_FILE, "prim%04d.txt", named_ini_files);
		sprintf (localini_file, "loca%04d.txt", named_ini_files);
		sprintf (WORKTODO_FILE, "work%04d.txt", named_ini_files);
		sprintf (RESFILE, "resu%04d.txt", named_ini_files);
		sprintf (RESFILEBENCH, "resu%04d.bench.txt", named_ini_files);
		sprintf (RESFILEJSON, "resu%04d.json.txt", named_ini_files);
		sprintf (SPOOL_FILE, "prim%04d.spl", named_ini_files);
		sprintf (LOGFILE, "prim%04d.log", named_ini_files);
		sprintf (SCREENLOGFILE, "screen%04d.log", named_ini_files);
	}

/* Let the user rename these files and pick a different working directory */

	IniGetString (INI_FILE, "WorkingDir", buf, sizeof(buf), NULL);
	IniGetString (INI_FILE, "local.ini", localini_file, 260, localini_file);
	IniGetString (INI_FILE, "worktodo.ini", WORKTODO_FILE, 260, WORKTODO_FILE);
	IniGetString (INI_FILE, "results.txt", RESFILE, 260, RESFILE);
	IniGetString (INI_FILE, "results.bench.txt", RESFILEBENCH, 260, RESFILEBENCH);
	IniGetString (INI_FILE, "results.json.txt", RESFILEJSON, 260, RESFILEJSON);
	IniGetString (INI_FILE, "prime.spl", SPOOL_FILE, 260, SPOOL_FILE);
	IniGetString (INI_FILE, "prime.log", LOGFILE, 260, LOGFILE);
	IniGetString (INI_FILE, "screen.log", SCREENLOGFILE, 260, SCREENLOGFILE);
	IniGetString (INI_FILE, "prime.ini", INI_FILE, 260, INI_FILE);
	if (buf[0]) {
		(void) _chdir (buf);
		IniFileReread (INI_FILE);
	}

/* In 30.10 build 5, we copy the settings from local.txt to prime.txt, then delete local.txt */

	{
		FILE *fd = fopen (localini_file, "r");
		if (fd != NULL) {
			char	section[80];
			char	line[1000];
			section[0] = 0;
			IniDelayWrites (INI_FILE);
			while (fgets (line, sizeof (line), fd)) {
				if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
				if (line[0] && line[strlen(line)-1] == '\r') line[strlen(line)-1] = 0;
				if (line[0] == 0) continue;
				// Locate section headers
				if (line[0] == '[') {
					strcpy (section, line+1);
					if (strchr (section, ']') != NULL) *strchr (section, ']') = 0;
					continue;
				}
				// Write line as a comment
				if (section[0]) IniSectionWriteString (INI_FILE, section, NULL, line);
				else IniWriteString (INI_FILE, NULL, line);
			}
			fclose (fd);
			_unlink (localini_file);
			IniResumeImmediateWrites (INI_FILE);
			IniFileReread (INI_FILE);
		}
	}

/* In 30.10 build 5, we moved many settings to individual sections of prime.txt */

	renameIniKey (NULL, "WorkerThreads", KEY_NumWorkers);			// Renamed in 30.10b5
	renameIniKey (NULL, "NoMoreWork", KEY_QuitGIMPS);			// Renamed in 30.11b1
	renameIniKey (NULL, "BenchAllComplex", "BenchNegacyclic");		// Renamed in 30.16b2

	IniWriteString (INI_FILE, "RollingAverageIsFromV27", NULL);		// Deprecated 30.10b5
	IniWriteString (INI_FILE, "V24OptionsConverted", NULL);			// Deprecated 30.10b5
	IniWriteString (INI_FILE, "AskedAboutMemory", NULL);			// Deprecated long before 30.10b5
	IniWriteString (INI_FILE, "Advanced", NULL);				// Deprecated long before 30.10b5
	IniWriteString (INI_FILE, "Affinity", NULL);				// Deprecated well before 30.10b5
	IniWriteString (INI_FILE, "W0", NULL);					// Deprecated 30.10b5, I don't believe this Windows-only setting was ever used
	IniWriteString (INI_FILE, "OldStyleSoftCrossover", NULL);		// Deprecated 30.12
	IniWriteString (INI_FILE, "SoftCrossover", NULL);			// Deprecated 30.12
	IniWriteString (INI_FILE, "SoftCrossoverAdjust", NULL);			// Deprecated 30.12

	moveToSection (SEC_Internals, KEY_V30OptionsConverted);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_OldCpuSpeed);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_NewCpuSpeed);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_NewCpuSpeedCount);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_RollingAverage);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_RollingHash);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_RollingStartTime);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_RollingCompleteTime);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_CertDailyCPURemaining);		// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_CertDailyMBRemaining);		// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_CertDailyRemainingLastUpdate);	// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrUID);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrComputerName);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrP00);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO1);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO2);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO3);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO4);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO5);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO6);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO7);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO8);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_SrvrPO9);				// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_LastEndDatesSent);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_WGUID_version);			// Moved here 30.10b5
	moveToSection (SEC_Internals, KEY_CertErrorCount);			// Moved here 30.10b5

	moveToSection (SEC_PrimeNet, KEY_DialUp);				// Moved here 30.10b5
	moveToSection (SEC_PrimeNet, KEY_ProxyHost);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_ProxyUser);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_ProxyPass);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_ProxyMask);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_Debug);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_UploadRateLimit);			// Has always been here
	moveToSection (SEC_PrimeNet, KEY_UploadStartTime);			// Has always been here
	moveToSection (SEC_PrimeNet, KEY_UploadEndTime);			// Has always been here
	moveToSection (SEC_PrimeNet, KEY_DownloadDailyLimit);			// Has always been here
	moveToSection (SEC_PrimeNet, KEY_DownloadRateLimit);			// Has always been here
	moveToSection (SEC_PrimeNet, KEY_ProofUploads);				// Has always been here
	moveToSection (SEC_PrimeNet, KEY_ProofHashLength);			// Has always been here
	//moveToSection (SEC_PrimeNet, KEY_ProofPower);				// Cannot move, appears in both main and PrimeNet section
	//moveToSection (SEC_PrimeNet, KEY_ProofPowerMult);			// Cannot move, appears in both main and PrimeNet section
	moveToSection (SEC_PrimeNet, KEY_UploadChunkSize);			// Has always been here

	moveToSection (SEC_Windows, KEY_MergeWindows);				// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_TrayIcon);				// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_HideIcon);				// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_Left);					// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_Right);					// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_Top);					// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_Bottom);				// Moved here 30.10b5
	moveToSection (SEC_Windows, KEY_ExitOnX);				// Moved here 30.10b5
	moveToSection (SEC_Windows, "W%d");					// Moved here 30.10b5

/* Perform other one-time initializations */

	LoadPrimenet ();
	init_spool_file_and_comm_code ();
	init_timed_event_handler ();

/* Create and name the main window */

	MERGE_WINDOWS = (int) IniSectionGetInt (INI_FILE, SEC_Windows, KEY_MergeWindows, MERGE_MAINCOMM_WINDOWS);
	create_window (MAIN_THREAD_NUM);
	base_title (MAIN_THREAD_NUM, "Main thread");

/* Set error handler for printing errors while reading INI files once the main window is created. */
/* Yes, we did ignore errors above reading prime.txt.  However, we will reread prime.txt */
/* and will print the error message then. */

	IniSetErrorCallback (ini_error_handler);

/* Output a startup message */

	sprintf (buf, "Mersenne number primality test program version %s build %s\n", VERSION, BUILD_NUM);
	OutputStr (MAIN_THREAD_NUM, buf);

/* Now that we have the proper file name, read the ini file into global variables. */

	readIniFiles ();

/* Generate the computer's UID if none exists */

	if (!COMPUTER_GUID[0]) generate_computer_guid ();

/* Calculate the hopefully-never-changing Windows GUID */

	calc_windows_guid ();

/* A stress tester ceases to be a stress tester if he ever turns on */
/* primenet or has work in his worktodo.txt file */

	if (STRESS_TESTER == 1 && (USE_PRIMENET || WORKTODO_COUNT)) {
		STRESS_TESTER = 0;
		IniWriteInt (INI_FILE, "StressTester", 0);
	}

/* Output our calculated CPU architecture and characteristics */

	sprintf (buf, "Optimizing for CPU architecture: %s, ",
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_PRE_SSE2 ? "Pre-SSE2" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_PENTIUM_4 ? "Pentium 4" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_PENTIUM_M ? "Pentium M" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_CORE ? "Core Solo/Duo" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_CORE_2 ? "Core 2" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_CORE_I7 ? "Core i3/i5/i7" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_PHI ? "Xeon Phi" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_ATOM ? "Atom" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_INTEL_OTHER ? "Unknown Intel" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K8 ? "AMD K8" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K10 ? "AMD K10" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_BULLDOZER ? "AMD Bulldozer" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_ZEN ? "AMD Zen" :
		 CPU_ARCHITECTURE == CPU_ARCHITECTURE_OTHER ? "Not Intel and not AMD" : "Undefined");
	strcat (buf, "L2 cache size: ");
	if (CPU_NUM_L2_CACHES <= 0) strcat (buf, "unknown");
	if (CPU_NUM_L2_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L2_CACHES);
	if (CPU_NUM_L2_CACHES >= 1) {
		if ((CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES) & 0x3FF)
			sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES);
		else
			sprintf (buf + strlen (buf), "%d MB", CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES / 1024);
	}
	if (CPU_NUM_L3_CACHES > 0) {
		strcat (buf, ", L3 cache size: ");
		if (CPU_NUM_L3_CACHES > 1) sprintf (buf + strlen (buf), "%dx", CPU_NUM_L3_CACHES);
		if ((CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES) & 0x3FF)
			sprintf (buf + strlen (buf), "%d KB", CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES);
		else
			sprintf (buf + strlen (buf), "%d MB", CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES / 1024);
	}
	strcat (buf, "\n");
	OutputStr (MAIN_THREAD_NUM, buf);
	if (!OS_CAN_SET_AFFINITY) OutputStr (MAIN_THREAD_NUM, "OS does not support setting CPU affinity.\n");

/* Start some initial timers */

	add_timed_event (TE_ROLLING_AVERAGE, 6*60*60);
	if (IniGetInt (INI_FILE, "AutoBench", 1)) {
		time_t	current_time;
		struct tm *x;
		int	seconds;
		gwevent_init (&AUTOBENCH_EVENT);
		time (&current_time);
		x = localtime (&current_time);
		if (x->tm_hour < 4)
			seconds = (5 - x->tm_hour) * 60 * 60;	// Start benchmark around 5AM today
		else
			seconds = (29 - x->tm_hour) * 60 * 60;	// Start benchmark around 5AM tomorrow
		add_timed_event (TE_BENCH, seconds);
	}

/* Start the proof uploading thread */

	if (IniSectionGetInt (INI_FILE, SEC_PrimeNet, KEY_ProofUploads, 1))
		gwthread_create (&UPLOAD_THREAD, &proofUploader, NULL);
}

/* Init the communications code.  We used to do this at the end of */
/* nameAndReadIniFiles, but running mprime with the -s or -t argument */
/* caused spurious creation of a prime.spl file. */

void initCommCode (void) {

/* Start or stop the communication timers.  This needs to be called */
/* every time the INI file is read in case there have been changes to */
/* the USE_PRIMENET or MANUAL_COMM variables. */

//bug - do this on all rereads of prime.txt?  If so, only after comm windows
//bug - is created (and I'm not sure the comm window should be recreated
//bug - on every prime.txt reread (and what of the windows bugs where
//bug - calling create_window from other than the main thread can hang
	set_comm_timers ();

/* Tell the server about any changed program options */

	spoolMessage (PRIMENET_PROGRAM_OPTIONS, NULL);

/* If we're using primenet and we don't know the userid, then send an */
/* update computer message to get the userid. */

	if (USE_PRIMENET && USERID[0] == 0)
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
}

/* Guess a good default value for number of workers based on NUMA / cache information from hwloc */

int good_default_for_num_workers (void)
{
	int	cores_per_node;

/* Default to roughly 4 cores per worker */

	cores_per_node = HW_NUM_COMPUTE_CORES / HW_NUM_COMPUTE_THREADING_NODES;
	return (HW_NUM_COMPUTE_THREADING_NODES * (cores_per_node <= 6 ? 1 : divide_rounding_up (cores_per_node, 4)));
}

/* Read CoresPerTest values.  This may require upgrading ThreadsPerTest to CoresPerTest. */
/* This INI setting changed from threads to cores in version 29.1. */

void read_cores_per_test (void)
{
	int	i, all_the_same;
	unsigned int temp[MAX_NUM_WORKERS];

/* Get the old ThreadsPerTest values that we used to support */

	PTOGetAll (INI_FILE, "ThreadsPerTest", temp, 0);

/* If we found some old settings then delete them from the INI file, */
/* convert from threads to cores as best we can, and write out the new settings */

	if (temp[0] != 0) {
		int	total_threads, trim;
		// Sum up the number of threads, see if each worker has the same number of threads
		for (i = 0, total_threads = 0; i < (int) NUM_WORKERS; i++) {
			if (temp[i] <= 0) temp[i] = 1;  // In theory, can't happen
			total_threads += temp[i];
		}
		// If there are more threads than cores, then some hyperthreading was probably
		// going on.  In the new scheme of things using hyperthreading is handled via
		// a different INI setting.  Reduce the array values until we are not
		// oversubscribing cores.
		for (i = 0; total_threads > (int) HW_NUM_CORES && i < (int) NUM_WORKERS; i++) {
			trim = temp[i] / 2;
			if (total_threads - trim < (int) HW_NUM_CORES) trim = total_threads - HW_NUM_CORES;
			temp[i] -= trim;
			total_threads -= trim;
		}
		// See if each worker has the same number of cores
		for (i = 0, all_the_same = TRUE; i < (int) NUM_WORKERS; i++) {
			if (i && temp[i] != temp[i-1]) all_the_same = FALSE;
		}
		// Write out the new settings
		memset (CORES_PER_TEST, 0xFF, sizeof (CORES_PER_TEST));
		if (all_the_same)
			PTOSetAll (INI_FILE, "CoresPerTest", NULL, CORES_PER_TEST, temp[0]);
		else for (i = 0; i < (int) NUM_WORKERS; i++)
			PTOSetOne (INI_FILE, "CoresPerTest", NULL, CORES_PER_TEST, i, temp[i]);
		// Clear old settings
		PTOSetAll (INI_FILE, "ThreadsPerTest", NULL, temp, 0);
		IniWriteString (INI_FILE, "ThreadsPerTest", NULL);
	}

/* Read in the CoresPerTest values that we support as of version 29.1 */

	PTOGetAll (INI_FILE, "CoresPerTest", CORES_PER_TEST, 0);

/* If we did not find any settings, use hwloc's information to give us a good default setting. */
/* For example, consider a dual CPU Xeon system with 9 cores per CPU.  A good setting for four workers */
/* would be 4 cores, 5 cores, 4 cores, 5 cores.  That way, we do not have a PRP/LL test spanning across CPUs. */

	if (CORES_PER_TEST[0] == 0) {
		int	cores, workers, node;

/* Decide how many workers will run on each compute node.  Then distribute the cores among those workers. */

		i = 0;
		cores = HW_NUM_COMPUTE_CORES;
		workers = NUM_WORKERS;
		for (node = 0; node < (int) HW_NUM_COMPUTE_THREADING_NODES; node++) {
			// Make workers this node proportional to the fraction of total cores in this node.
			// In the final node, we must assign all the workers we have not yet figured out.  This happens
			// automatically as the final cores_this_node value must equal the unassigned compute cores remaining.
			int cores_this_node = get_cores_in_threading_node (node);
			int workers_this_node = divide_rounding (workers * cores_this_node, cores);
			// Keep track of the number of cores and workers remaining
			cores -= cores_this_node;
			workers -= workers_this_node;
			// Assign node cores to the node workers
			for ( ; workers_this_node; workers_this_node--) {
				temp[i] = cores_this_node / workers_this_node;
				cores_this_node -= temp[i];
				i++;
			}
		}
		// Assert that all workers and compute cores were assigned
		ASSERTG (workers == 0 && cores == 0);

		// See if each worker has the same number of cores
		for (i = 0, all_the_same = TRUE; i < (int) NUM_WORKERS; i++) {
			if (i && temp[i] != temp[i-1]) all_the_same = FALSE;
		}
		// Write out the new settings
		if (all_the_same)
			PTOSetAll (INI_FILE, "CoresPerTest", NULL, CORES_PER_TEST, temp[0]);
		else for (i = 0; i < (int) NUM_WORKERS; i++)
			PTOSetOne (INI_FILE, "CoresPerTest", NULL, CORES_PER_TEST, i, temp[i]);
	}

/* Sanity check the CoresPerTest.  In case user hand-edited prime.txt */

	for (i = 0; i < (int) NUM_WORKERS; i++) {
		if (CORES_PER_TEST[i] < 1) CORES_PER_TEST[i] = 1;
		if (CORES_PER_TEST[i] > HW_NUM_CORES) CORES_PER_TEST[i] = HW_NUM_CORES;
	}
}

/* Read or re-read the INI files & and do other initialization */

int readIniFiles (void)
{
	int	rc, temp;
	int	day_memory;
	int	night_memory;

/* Force the INI files to be reread, just in case they were hand edited. */
/* Incorporate any additions from .add files */

	IniFileReread (INI_FILE);
	incorporateIniAddFiles ();

/* Get the CPU type, speed, and capabilities. */

	getCpuInfo ();

/* Put commonly used options in global variables */

	IniGetString (INI_FILE, "ComputerGUID", COMPUTER_GUID, sizeof (COMPUTER_GUID), NULL);
	IniGetString (INI_FILE, "V5UserID", USERID, sizeof (USERID), NULL);
	IniGetString (INI_FILE, "ComputerID", COMPID, sizeof (COMPID), NULL);
	sanitizeString (COMPID);
	USE_PRIMENET = (int) IniGetInt (INI_FILE, "UsePrimenet", 0);
	DIAL_UP = (int) IniSectionGetInt (INI_FILE, SEC_PrimeNet, KEY_DialUp, 0);
	DAYS_OF_WORK = (unsigned int) IniGetInt (INI_FILE, "DaysOfWork", 3);
	if (DAYS_OF_WORK > 180) DAYS_OF_WORK = 180;

	CPU_WORKER_DISK_SPACE = IniGetFloat (INI_FILE, "WorkerDiskSpace", 6.0);
	if (CPU_WORKER_DISK_SPACE < 0.0) CPU_WORKER_DISK_SPACE = 0.0;
	if (CPU_WORKER_DISK_SPACE > 1000.0) CPU_WORKER_DISK_SPACE = 1000.0;

	CPU_HOURS = (unsigned int) IniGetInt (INI_FILE, "CPUHours", 24);
	if (CPU_HOURS < 1) CPU_HOURS = 1;
	if (CPU_HOURS > 24) CPU_HOURS = 24;

	ROLLING_AVERAGE = (unsigned int) IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingAverage, 1000);
	if (ROLLING_AVERAGE < 10) ROLLING_AVERAGE = 10;
	if (ROLLING_AVERAGE > 4000) ROLLING_AVERAGE = 4000;

	PRECISION = (unsigned int) IniGetInt (INI_FILE, "PercentPrecision", 2);
	if (PRECISION > 6) PRECISION = 6;

	CLASSIC_OUTPUT = IniGetInt (INI_FILE, "ClassicOutput", 0);
	OUTPUT_ROUNDOFF = IniGetInt (INI_FILE, "OutputRoundoff", 0);
	ITER_OUTPUT = IniGetInt (INI_FILE, "OutputIterations", 10000);
	if (ITER_OUTPUT > 999999999) ITER_OUTPUT = 999999999;
	if (ITER_OUTPUT <= 0) ITER_OUTPUT = 1;
	ITER_OUTPUT_RES = IniGetInt (INI_FILE, "ResultsFileIterations", 999999999);
	if (ITER_OUTPUT_RES > 999999999) ITER_OUTPUT_RES = 999999999;
	if (ITER_OUTPUT_RES < 1000) ITER_OUTPUT_RES = 1000;
	DISK_WRITE_TIME = IniGetInt (INI_FILE, "DiskWriteTime", 30);
	JACOBI_TIME = IniGetInt (INI_FILE, "JacobiErrorCheckingInterval", 12);
	if (JACOBI_TIME < 1) JACOBI_TIME = 1;
	MODEM_RETRY_TIME = (unsigned int) IniGetInt (INI_FILE, "NetworkRetryTime", 2);
	if (MODEM_RETRY_TIME < 1) MODEM_RETRY_TIME = 1;
	if (MODEM_RETRY_TIME > 300) MODEM_RETRY_TIME = 300;
	NETWORK_RETRY_TIME = (unsigned int)
		IniGetInt (INI_FILE, "NetworkRetryTime2",
			   MODEM_RETRY_TIME > 70 ? MODEM_RETRY_TIME : 70);
	if (NETWORK_RETRY_TIME < 1) NETWORK_RETRY_TIME = 1;
	if (NETWORK_RETRY_TIME > 300) NETWORK_RETRY_TIME = 300;

	DAYS_BETWEEN_CHECKINS = IniGetFloat (INI_FILE, "DaysBetweenCheckins", 1.0);
	if (DAYS_BETWEEN_CHECKINS > 7.0) DAYS_BETWEEN_CHECKINS = 7.0;				/* 7 day maximum */
	if (DAYS_BETWEEN_CHECKINS * 24.0 < 1.0) DAYS_BETWEEN_CHECKINS = (float) (1.0 / 24.0);	/* 1 hour minimum */
	SILENT_VICTORY = (int) IniGetInt (INI_FILE, "SilentVictory", 0);
	SILENT_VICTORY_PRP = (int) IniGetInt (INI_FILE, "SilentVictoryPRP", 1);
	RUN_ON_BATTERY = (int) IniGetInt (INI_FILE, "RunOnBattery", 1);
	BATTERY_PERCENT = (int) IniGetInt (INI_FILE, "BatteryPercent", 0);
	DEFEAT_POWER_SAVE = (int) IniGetInt (INI_FILE, "DefeatPowerSave", 1);

	STRESS_TESTER = (int) IniGetInt (INI_FILE, "StressTester", 99);
	temp = (int) IniGetInt (INI_FILE, "ErrorCheck", 0);
	ERRCHK = (temp != 0);
	NUM_WORKERS = IniGetInt (INI_FILE, KEY_NumWorkers, good_default_for_num_workers ());
	if (NUM_WORKERS < 1) NUM_WORKERS = 1;
	if (NUM_WORKERS > MAX_NUM_WORKERS) NUM_WORKERS = MAX_NUM_WORKERS;
	IniWriteInt (INI_FILE, KEY_NumWorkers, NUM_WORKERS); // Write in case future prime95 changes good_default_for_num_workers
	PRIORITY = (unsigned int) IniGetInt (INI_FILE, "Priority", 1);
	if (PRIORITY < 1) PRIORITY = 1;
	if (PRIORITY > 10) PRIORITY = 10;
	PTOGetAll (INI_FILE, "WorkPreference", WORK_PREFERENCE, 0);
	// Upgrade pre-v30 users from LL to PRP, combine LL-DC and PRP-DC into one work preference
	if (!IniSectionGetInt (INI_FILE, SEC_Internals, KEY_V30OptionsConverted, 0)) {
		int	i, all_the_same = TRUE;
		for (i = 0; i < MAX_NUM_WORKERS; i++) {
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_FIRST) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_FIRST;
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_10M) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_FIRST;
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_FIRST_NOFAC) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_FIRST;
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_WORLD_RECORD) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_WORLD_RECORD;
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_100M) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_100M;
			if (WORK_PREFERENCE[i] == PRIMENET_WP_LL_DBLCHK) WORK_PREFERENCE[i] = PRIMENET_WP_PRP_DBLCHK;
			if (i && WORK_PREFERENCE[i] != WORK_PREFERENCE[i-1]) all_the_same = FALSE;
		}
		if (all_the_same)
			PTOSetAll (INI_FILE, "WorkPreference", NULL, WORK_PREFERENCE, WORK_PREFERENCE[0]);
		else for (i = 0; i < (int) NUM_WORKERS; i++)
			PTOSetOne (INI_FILE, "WorkPreference", NULL, WORK_PREFERENCE, i, WORK_PREFERENCE[i]);
		spoolMessage (PRIMENET_PROGRAM_OPTIONS, NULL);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_V30OptionsConverted, 1);
	}
	read_cores_per_test ();		// Get CORES_PER_TEST array, may require upgrading old INI settings
	HYPERTHREAD_TF = IniGetInt (INI_FILE, "HyperthreadTF", OS_CAN_SET_AFFINITY);
	HYPERTHREAD_LL = IniGetInt (INI_FILE, "HyperthreadLL", 0);
	MANUAL_COMM = (int) IniGetInt (INI_FILE, "ManualComm", 0);
	HIDE_ICON = (int) IniSectionGetInt (INI_FILE, SEC_Windows, KEY_HideIcon, 0);
	TRAY_ICON = (int) IniSectionGetInt (INI_FILE, SEC_Windows, KEY_TrayIcon, 1);
	MERGE_WINDOWS = (int) IniSectionGetInt (INI_FILE, SEC_Windows, KEY_MergeWindows, MERGE_MAINCOMM_WINDOWS);

/* Convert old TwoBackupFiles boolean to new NumBackupFiles integer.  Old default */
/* was 2 save files, new default is 3 save files. */	

	temp = (int) IniGetInt (INI_FILE, "TwoBackupFiles", 2);
	NUM_BACKUP_FILES = (int) IniGetInt (INI_FILE, "NumBackupFiles", temp+1);
	NUM_JACOBI_BACKUP_FILES = (int) IniGetInt (INI_FILE, "JacobiBackupFiles", 2);

/* Convert the old DAY_MEMORY, NIGHT_MEMORY settings into the simpler, all-inclusive */
/* and more powerful MEMORY setting. */

	day_memory = IniGetInt (INI_FILE, "DayMemory", 0);
	night_memory = IniGetInt (INI_FILE, "NightMemory", 0);
	if (day_memory || night_memory) {
		int	day_start_time, day_end_time;
		day_memory = (day_memory < 0) ? physical_memory () + day_memory : day_memory;
		if (day_memory < 8) day_memory = 8;
		if ((unsigned long) day_memory > physical_memory () - 8) day_memory = physical_memory () - 8;
		night_memory = (night_memory < 0) ? physical_memory () + night_memory : night_memory;
		if (night_memory < 8) night_memory = 8;
		if ((unsigned long) night_memory > physical_memory () - 8) night_memory = physical_memory () - 8;
		day_start_time = IniGetInt (INI_FILE, "DayStartTime", 450);
		day_end_time = IniGetInt (INI_FILE, "DayEndTime", 1410);
		write_memory_settings (day_memory, night_memory, day_start_time, day_end_time);
	}
	read_mem_info ();

/* Get the option controlling which timer to use.  If the high resolution */
/* performance counter is not available on this machine, then add 10 to */
/* the RDTSC_TIMING value. */

	RDTSC_TIMING = IniGetInt (INI_FILE, "RdtscTiming", 1);
	if (RDTSC_TIMING < 10 && ! isHighResTimerAvailable ())
		RDTSC_TIMING += 10;

/* Other oddball options */

	TIMESTAMPING = IniGetInt (INI_FILE, "TimeStamp", 1);
	TIMESTAMPING_UTC = IniGetInt (INI_FILE, "TimeStampUTC", 0);
	LOGTIMESTAMPING = IniGetInt (INI_FILE, "LogTimeStamp", 1);
	LOGTIMESTAMPING_UTC = IniGetInt (INI_FILE, "LogTimeStampUTC", 0);
	SCREENLOG = IniGetInt (INI_FILE, "ScreenLog", 0);
	CUMULATIVE_TIMING = IniGetInt (INI_FILE, "CumulativeTiming", 0);
	CUMULATIVE_ROUNDOFF = IniGetInt (INI_FILE, "CumulativeRoundoff", 1);
	SEQUENTIAL_WORK = IniGetInt (INI_FILE, "SequentialWorkToDo", -1);
	WELL_BEHAVED_WORK = IniGetInt (INI_FILE, "WellBehavedWork", 0);

	read_pause_info ();
	read_load_average_info ();

	INTERIM_FILES = IniGetInt (INI_FILE, "InterimFiles", 0);
	INTERIM_RESIDUES = IniGetInt (INI_FILE, "InterimResidues", INTERIM_FILES);
	HYPERTHREADING_BACKOFF = IniGetInt (INI_FILE, "HyperthreadingBackoff", 0);//bug	   CPU_HYPERTHREADS <= 1 ? 0 : 30);

/* Option to slow down the program by sleeping after every iteration.  You */
/* might use this on a laptop or a computer running in a hot room to keep */
/* temperatures down and thus reduce the chance of a hardware error.  */

	THROTTLE_PCT = IniGetInt (INI_FILE, "Throttle", 0);

/* Now read the work-to-do file */

	rc = readWorkToDoFile ();
	if (rc) {
		OutputStr (MAIN_THREAD_NUM, "Error reading worktodo.txt file\n");
		return (rc);
	}

/* Return success */

	return (0);
}


/****************************************************************************/
/*               Utility routines to work with ".add" files                 */
/****************************************************************************/

/* See if an "add file" file exists.  An add file lets the user create a prime.add file to overwrite/append options while the program is running. */
/* This is especially handy for workstations that are not physically accessible but their file systems are by network.  If this feature was not available, */
/* the only safe method for updating would be to stop the program, edit the .txt file, and restart the program. */

int addFileExists (void)
{
	char	filename[80];
	char	*dot;

	strcpy (filename, INI_FILE);
	dot = strrchr (filename, '.');
	if (dot != NULL) {
		strcpy (dot, ".add");
		if (fileExists (filename)) return (TRUE);
		strcpy (dot, ".add.txt");
		if (fileExists (filename)) return (TRUE);
	}
	return (FALSE);
}

/* Merge all INI ".add files" into their corresponding base files */

void incorporateIniAddFiles (void)
{
	char	filename[80];
	char	*dot;

/* Merge additions to prime.txt */

	strcpy (filename, INI_FILE);
	dot = strrchr (filename, '.');
	if (dot != NULL) {
		strcpy (dot, ".add");
		if (fileExists (filename))
			IniAddFileMerge (INI_FILE, filename, NULL);
		strcpy (dot, ".add.txt");
		if (fileExists (filename))
			IniAddFileMerge (INI_FILE, filename, NULL);
	}
}

/* Merge worktodo.add file into the worktodo.txt file */

int incorporateWorkToDoAddFile (void)
{
static	int	worktodo_add_disabled = FALSE;
	char	filename[80];
	char	*dot;
	int	rc;
	FILE	*fd;
	unsigned int tnum;
	char	line[2048];

/* If add files have been disabled (see below) then we're all done */

	if (worktodo_add_disabled) return (0);

/* Merge additions to worktodo.txt */

	strcpy (filename, WORKTODO_FILE);
	dot = strrchr (filename, '.');
	if (dot == NULL) return (0);

/* Open the worktodo.add file, it is OK if this file does not exist. */

	strcpy (dot, ".add");
	fd = fopen (filename, "r");
	if (fd == NULL) {
		strcpy (dot, ".add.txt");
		fd = fopen (filename, "r");
		if (fd == NULL) return (0);
	}

/* As an ugly kludge, we append lines from worktodo.add as comments in the */
/* in-memory version of worktodo.txt.  Later we will write worktodo.txt to */
/* disk and reprocess it entirely.  Loop processing each worktodo.add line */

	gwmutex_lock (&WORKTODO_MUTEX);
	tnum = 0;
	while (fgets (line, sizeof (line), fd)) {
		struct work_unit *w;
		int	rc;

/* Remove trailing CRLFs */

		if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
		if (line[0] && line[strlen(line)-1] == '\r') line[strlen(line)-1] = 0;
		if (line[0] == 0) continue;

/* If this is a section header find the matching section header in worktodo.txt.  If no match is found, add this to the */
/* first empty thread or the very last thread. */

		if (line[0] == '[') {
			struct work_unit *w;
			for (tnum = 0; ; tnum++) {
				w = WORK_UNITS[tnum].first;
				if (w == NULL) {
					if (tnum) break;
				} else {
					if (w->work_type == WORK_NONE && _stricmp (w->comment, line) == 0) break;
				}
				if (tnum == NUM_WORKERS - 1) {
					w = NULL;
					break;
				}
			}
			if (w != NULL) continue;
		}

/* Allocate a work unit structure */

		w = (struct work_unit *) malloc (sizeof (struct work_unit));
		if (w == NULL) goto nomem;

/* Parse the new line */

		rc = parseWorkToDoLine (line, w);
		if (rc) goto retrc;

/* Grow the work_unit array if necessary and add this entry */

		rc = addToWorkUnitArray (tnum, w, ADD_TO_END);
		if (rc) goto retrc;
	}

/* Close the file, free the lock and return success */

	fclose (fd);
	gwmutex_unlock (&WORKTODO_MUTEX);

/* Write the combined worktodo.txt file */

	WORKTODO_CHANGED = TRUE;
	rc = writeWorkToDoFile (TRUE);
	if (rc) return (rc);

/* Delete the worktodo.add file.  If file exists after we tried to delete it */
/* then permanently disable worktodo.add processing (to avoid an infinite */
/* loop growing and growing the worktodo file with redundant work! */

	_unlink (filename);
	if (fileExists (filename)) {
		OutputBoth (MAIN_THREAD_NUM, "ERROR: Can't delete worktodo.add file\n");
		worktodo_add_disabled = TRUE;
	}

/* Reprocess the combined and freshly written worktodo.txt file.  Spool message to check the work queue, */
/* this will get assignment IDs and send completion dates for the newly added work. */

	rc = readWorkToDoFile ();
	spoolMessage (MSG_CHECK_WORK_QUEUE, NULL);
	return (rc);

/* Handle an error during the reading of the add file */

nomem:	rc = OutOfMemory (MAIN_THREAD_NUM);
retrc:	fclose (fd);
	gwmutex_unlock (&WORKTODO_MUTEX);
	return (rc);
}

/****************************************************************************/
/*          Utility routines to work with per-worker options (PTO)          */
/****************************************************************************/

void PTOGetAll (
	const char *ini_filename,	/* Ini file containing the options */
	const char *keyword,		/* Ini file keyword */
	unsigned int *array,		/* Options array */
	unsigned int def_val)		/* Default value */
{
	int	i, global_val;
	unsigned int seconds;
	char	section_name[32];

/* Copy the global option setting to the entire array.  Used timed INI routines starting in 30.12 to allow for timed CoresPerTest at James' request. */

	global_val = IniGetTimedInt (ini_filename, keyword, def_val, &seconds);
	if (seconds) add_timed_event (TE_READ_INI_FILE, seconds);
	for (i = 0; i < MAX_NUM_WORKERS; i++) {
		array[i] = global_val;
	}

/* Now look for any section specific overrides */

	for (i = 0; i < MAX_NUM_WORKERS; i++) {
		sprintf (section_name, "Worker #%d", i+1);
		array[i] = IniSectionGetTimedInt (ini_filename, section_name, keyword, array[i], &seconds);
		if (seconds) add_timed_event (TE_READ_INI_FILE, seconds);
	}
}

void PTOSetAll (
	const char *ini_filename,	/* Ini file containing the options */
	const char *keyword,		/* Ini file keyword */
	const char *shadow_keyword,	/* Ini file keyword for value the server has stored for this option */
	unsigned int *array,		/* Options array */
	unsigned int new_val)		/* New option value */
{
	int	i;
	char	section_name[32];

/* Copy the global option setting to the entire array */

	IniWriteInt (ini_filename, keyword, new_val);
	for (i = 0; i < MAX_NUM_WORKERS; i++) {
		array[i] = new_val;
	}
	if (shadow_keyword != NULL) IniSectionWriteInt (INI_FILE, SEC_Internals, shadow_keyword, new_val);

/* Now clear all section specific overrides */

	for (i = 0; i < MAX_NUM_WORKERS; i++) {
		sprintf (section_name, "Worker #%d", i+1);
		IniSectionWriteString (ini_filename, section_name, keyword, NULL);
		if (shadow_keyword != NULL) IniSectionWriteString (INI_FILE, section_name, shadow_keyword, NULL);
	}
}

void PTOSetOne (
	const char *ini_filename,	/* Ini file containing the options */
	const char *keyword,		/* Ini file keyword */
	const char *shadow_keyword,	/* Ini file keyword for value the server has stored for this option */
	unsigned int *array,		/* Options array */
	int	tnum,			/* Worker number */
	unsigned int new_val)		/* New option value */
{
	char	section_name[32];

/* Will changing this option cause us to switch from one global setting to individual settings on each worker? */

	if (PTOIsGlobalOption (array)) {
		int	i;

/* If option has not changed, then do nothing */

		if (array[tnum] == new_val) return;

/* Delete the global option and set the option for each worker. */

		IniWriteString (ini_filename, keyword, NULL);
		if (shadow_keyword != NULL) IniSectionWriteString (INI_FILE, SEC_Internals, shadow_keyword, NULL);
		for (i = 0; i < (int) NUM_WORKERS; i++) {
			sprintf (section_name, "Worker #%d", i+1);
			IniSectionWriteInt (ini_filename, section_name, keyword, array[i]);
			if (shadow_keyword != NULL) IniSectionWriteInt (INI_FILE, section_name, shadow_keyword, array[i]);
		}
	}

/* Set the option for just this one worker */

	array[tnum] = new_val;
	sprintf (section_name, "Worker #%d", tnum+1);
	IniSectionWriteInt (ini_filename, section_name, keyword, new_val);
	if (shadow_keyword != NULL) IniSectionWriteInt (INI_FILE, section_name, shadow_keyword, new_val);
}

int PTOIsGlobalOption (
	unsigned int *array)		/* Options array */
{
	for (int i = 1; i < (int) NUM_WORKERS; i++)
		if (array[i-1] != array[i]) return (FALSE);
	return (TRUE);
}

int PTOHasOptionChanged (
	const char *shadow_keyword,	/* Ini file keyword for value the server has stored for this option */
	unsigned int *array,		/* Options array */
	int	tnum)			/* Worker number */
{
	char	section_name[32];

/* If this is the worker number that tells us we are dealing global options */
/* then return TRUE if this is a globally set option and it has changed. */

	if (tnum == -1) return (PTOIsGlobalOption (array) && array[0] != IniSectionGetInt (INI_FILE, SEC_Internals, shadow_keyword, -1));

/* Otherwise, return TRUE if this is not a globally set option and it has changed. */

	sprintf (section_name, "Worker #%d", tnum+1);
	return (!PTOIsGlobalOption (array) && array[tnum] != IniSectionGetInt (INI_FILE, section_name, shadow_keyword, -1));
}


/****************************************************************************/
/*                Utility routines to output messages                       */
/****************************************************************************/

/* Format the current time according to user preferences */

void timestamp (
	char	*buf,
	int	type)		// 0=screen, 1=logfile
{
	char	user_defined_fmt[100];
	const char *fmtstr;
	time_t	this_time;
	int	code, utc;

// User can specify different screen and logfile formats.  Also, the two have different defaults.
	
	if (type == 0) {	// Format for screen
		fmtstr = "%b %e %H:%M";				/* Default format compatible with pre-30.19b6 */
		code = TIMESTAMPING;
		utc = TIMESTAMPING_UTC;
	} else {		// Format for logfiles
		fmtstr = "%a %b %e %H:%M:%S %Y";		/* Default format compatible with pre-30.19b6 */
		code = LOGTIMESTAMPING;
		utc = LOGTIMESTAMPING_UTC;
	}
	if (code == 0) { *buf = 0; return; }			/* No timestamping */
	if (code == 2) fmtstr = "%b %e %H:%M:%S";		/* Screen formatting option supported pre-30.19b6 */
	else if (code == 3) fmtstr = "%H:%M";			/* Screen formatting option supported pre-30.19b6 */
	else if (code == 4) fmtstr = "%H:%M:%S";		/* Screen formatting option supported pre-30.19b6 */
	else if (code == 5) fmtstr = "%Y-%m-%d %H:%M";		/* New in version 30.19b6, primarily for logfiles */
	else if (code == 6) fmtstr = "%Y-%m-%d %H:%M:%S";	/* New in version 30.19b6, primarily for logfiles */
	else if (code == 7) {					/* New in version 30.19b6 */
		IniGetString (INI_FILE, type == 0 ? "TimestampFormat" : "LogTimestampFormat", user_defined_fmt, sizeof (user_defined_fmt), fmtstr);
		fmtstr = user_defined_fmt;
	}

	/* Format local time or UTC time */
	time (&this_time);
	if (utc) strftime (buf, 80, fmtstr, gmtime (&this_time));
	else strftime (buf, 80, fmtstr, localtime (&this_time));
}

/* Intercept output to the screen for optional logging to a file */

void OutputStrInternal (
	int	thread_num,
	const char *buf)
{
static	int	last_char_out_was_newline = TRUE;
	
	RealOutputStr (thread_num, buf);
	if ((SCREENLOG & 0xF) == 1 ||					// Log everything
	    ((SCREENLOG & 0x2) && thread_num >= 0) ||			// Log workers
	    ((SCREENLOG & 0x4) && thread_num == COMM_THREAD_NUM) ||	// Log communications window
	    ((SCREENLOG & 0x8) && thread_num == MAIN_THREAD_NUM)) {	// Log main window
		int fd = _open (SCREENLOGFILE, _O_TEXT | _O_RDWR | _O_CREAT | _O_APPEND, CREATE_FILE_ACCESS);
		if (fd >= 0) {
			if (last_char_out_was_newline && !(SCREENLOG & 0x10)) {
				char	prefix[40];
				if (thread_num == MAIN_THREAD_NUM) strcpy (prefix, "[Main window");
				else if (thread_num == COMM_THREAD_NUM) strcpy (prefix, "[Comm window");
				else if (NUM_WORKERS == 1 && WORKERS_ACTIVE == 1) strcpy (prefix, "[Worker");
				else sprintf (prefix, "[Worker #%d", thread_num+1);
				strcat (prefix, "] ");
				_write (fd, prefix, (unsigned int) strlen (prefix));
			}
			_write (fd, buf, (unsigned int) strlen (buf));
			_close (fd);
		}
		last_char_out_was_newline = (buf[strlen(buf)-1] == '\n');
	}
}

/* Output string to screen or results file */

void OutputSomewhere (
	int	thread_num,
	const char *buf)
{
	if (NO_GUI) writeResults (buf);
	else OutputStr (thread_num, buf);
}

/* Output string to both the screen and results file */

EXTERNC
void OutputBoth (
	int	thread_num,
	const char *buf)
{
	OutputStr (thread_num, buf);
	writeResults (buf);
}

/* Output string to both the screen and results.bench file */

void OutputBothBench (
	int	thread_num,
	const char *buf)
{
	OutputStr (thread_num, buf);
	writeResultsBench (buf);
}

/* Output errno and useful description to both the screen and results file */

#include "errno.h"

void OutputBothErrno (
	int	thread_num)
{
	char	buf[1024];
#ifdef WIN32
	int	errnum;
	unsigned long dos_errnum;
	char	errtxt[512];

	_get_errno (&errnum);
	if (errnum) {
		strerror_s (errtxt, sizeof (errtxt), errnum);
		sprintf (buf, "Errno: %d, %s\n", errnum, errtxt);
		OutputBoth (thread_num, buf);
		_set_errno (0);
	}

	_get_doserrno (&dos_errnum);
	if (dos_errnum) {
		sprintf (buf, "DOSerrno: %ld\n", dos_errnum);
		OutputBoth (thread_num, buf);
		_set_doserrno (0);
	}
#else
	if (errno) {
		sprintf (buf, "Errno: %d, %s\n", errno, strerror (errno));
		OutputBoth (thread_num, buf);
	}
#endif
}

/* Output string to screen.  Prefix it with an optional timestamp. */

void OutputStr (
	int	thread_num,
	const char *buf)
{

/* Grab a lock so that multiple threads cannot output at the same time. */
/* Simultaneous output would be real bad on OS's that output all this data */
/* to a single screen/window (such as a Linux command line implementation). */

	gwmutex_lock (&OUTPUT_MUTEX);

/* Format a timestamp.  Prefix every line in the input buffer with the timestamp. */

	if (TIMESTAMPING) {
		char	tmpbuf[200], fmtbuf[202];

		timestamp (tmpbuf, 0);
		sprintf (fmtbuf, "[%s] ", tmpbuf);

/* Output the prefix for every line in the buffer */ 

		do {
			const char *eol;

			eol = strchr (buf, '\n');
			if (eol != NULL) eol++;
			else eol = buf + strlen (buf);
			OutputStrInternal (thread_num, fmtbuf);
			while (buf != eol) {
				int	len;
				len = (int) (eol - buf);
				if (len >= sizeof (tmpbuf)) len = sizeof (tmpbuf) - 1;
				memcpy (tmpbuf, buf, len);
				tmpbuf[len] = 0;
				OutputStrInternal (thread_num, tmpbuf);
				buf += len;
			}
		} while (*buf);
	}

/* No timestamp - just output the possibly multi-line buffer. */

	else
		OutputStrInternal (thread_num, buf);

/* Free the lock and return */

	gwmutex_unlock (&OUTPUT_MUTEX);
}

/* Output string to screen without prefixing a timestamp.  Only used when outputting a line in chunks (we do this when benchmarking). */

void OutputStrNoTimeStamp (
	int	thread_num,
	const char *buf)
{
	gwmutex_lock (&OUTPUT_MUTEX);
	OutputStrInternal (thread_num, buf);
	gwmutex_unlock (&OUTPUT_MUTEX);
}

/* Output an out-of-memory error */

int OutOfMemory (
	int thread_num)
{
	OutputStr (thread_num, "Out of memory!\n");
	return (STOP_OUT_OF_MEM);
}

/* Append exponent or kbnc to JSON message */

void JSONaddExponent (
	char	*JSONbuf,
	struct work_unit *w)
{
	if (w->k == 1.0 && w->b == 2 && w->c == -1) sprintf (JSONbuf+strlen(JSONbuf), ", \"exponent\":%lu", w->n);
	else sprintf (JSONbuf+strlen(JSONbuf), ", \"k\":%.0f, \"b\":%lu, \"n\":%lu, \"c\":%ld", w->k, w->b, w->n, w->c);
}

void JSONaddExponentKnownFactors (
	char	*JSONbuf,
	struct work_unit *w)
{
	if (w->known_factors != NULL) {
		char	fac_string[1210];
		char	*in, *out;
		fac_string[0] = 0;
		// Copy known factors changing commas to quote-comma-quote
		for (in = w->known_factors, out = fac_string; ; in++) {
			if (*in == ',') {
				strcpy (out, "\",\"");
				out += 3;
			} else if (out - fac_string > 1200) {
				strcpy (out, "...");
				break;
			} else
				*out++ = *in;
			if (*in == 0) break;
		}
		sprintf (JSONbuf+strlen(JSONbuf), ", \"known-factors\":[\"%s\"]", fac_string);
	}
}

/* Append program and timestamp to JSON message */

void JSONaddProgramTimestamp (
	char	*JSONbuf)
{
	time_t rawtime;

	sprintf (JSONbuf+strlen(JSONbuf), ", \"program\":{\"name\":\"Prime95\", \"version\":\"%s\", \"build\":%s, \"port\":%d}", VERSION, BUILD_NUM, PORT);
	time (&rawtime);
	strftime (JSONbuf+strlen(JSONbuf), 80, ", \"timestamp\":\"%Y-%m-%d %H:%M:%S\"", gmtime (&rawtime));
}

/* Append user, computer, assignment ID to JSON message */

void JSONaddUserComputerAID (
	char	*JSONbuf,
	struct work_unit *w)
{
	if (USERID[0]) sprintf (JSONbuf+strlen(JSONbuf), ", \"user\":\"%s\"", USERID);
	if (COMPID[0]) sprintf (JSONbuf+strlen(JSONbuf), ", \"computer\":\"%s\"", COMPID);
	if (w != NULL && w->assignment_uid[0]) sprintf (JSONbuf+strlen(JSONbuf), ", \"aid\":\"%s\"", w->assignment_uid);
}

/****************************************************************************/
/*               Routines to process worktodo.txt files                     */
/****************************************************************************/

/* The worktodo structures are accessed by workers, the communication */
/* thread, the timer threads, and the GUI thread.  We must be VERY CAREFUL */
/* with our locking scheme to make sure there are no crashes or hangs. */

/* The simplest scheme just locks on entry to each of these routines. */
/* This solves some basic problems such as two threads writing the worktodo */
/* file at the same time.  However, more difficult problems still exist. */
/* For example, a worker could delete the work unit while the */
/* GUI (Test/Status) or comm thread (sending completion dates) try to */
/* has a pointer to the work unit structure.  A dereference after the */
/* delete will crash. */

/* The scheme I came up with increments a per work unit counter that */
/* indicates the work unit is in use.  Deletes are not allowed while the */
/* work unit is in use. */

/* We complicate this scheme by differentiating between in-use by a worker */
/* thread (it could take a very long time to decrement the counter) and */
/* access by the comm or GUI threads (these should be very quick accesses). */

/* Finally, the comm thread has a work-unit in use when it sends a message */
/* to the server.  If this hangs, the timer code must decrement the in-use */
/* counter and kill the thread (or leave it in a hung state). */


/* Count commas in a string */

unsigned int countCommas (
	const char *p)
{
	unsigned int cnt;
	for (cnt = 0; ; cnt++) {
		p = strchr (p, ',');
		if (p == NULL) break;
		p++;
	}
	return (cnt);
}

/* Do some more initialization of work_unit fields.  These values do */
/* not appear in the worktodo.txt file, but need initializing in a common place. */

void auxiliaryWorkUnitInit (
	struct work_unit *w)
{

/* Compute factor_to if not set already */

	if ((w->work_type == WORK_FACTOR || w->work_type == WORK_TEST || w->work_type == WORK_DBLCHK || w->work_type == WORK_PRP) && w->factor_to == 0.0)
		w->factor_to = factorLimit (w);

/* Initialize the number of LL tests saved.  LL tests are mostly deprecated thanks to PRP with proofs. */
/* For PRP work, the worktodo file specifies the number of tests saved -- don't overwrite it. */

	if (w->work_type == WORK_TEST) w->tests_saved = 2.0;
	if (w->work_type == WORK_DBLCHK) w->tests_saved = 1.0;

/* Guard against wild tests_saved values.  Huge values will cause guess_pminus1_bounds to run for a very long time. */

	if (w->tests_saved > 100) w->tests_saved = 100;
}

/* Fill in a work unit's stage and percentage complete based on any save files. */

void pct_complete_from_savefile (
	struct work_unit *w)
{
	int	fd, res;
	uint32_t version;
	char	filename[32];

/* Generate the save file name */

	tempFileName (w, filename);

/* See if there is an intermediate file.  If there is read it to get our */
/* stage and percent complete.  This is a little tricky for LL and PRP tests */
/* which could have 3 different types of save files (LL/PRP, P-1, or trial */
/* factoring). */

	for ( ; ; ) {
		fd = _open (filename, _O_BINARY | _O_RDONLY);
		if (fd <= 0) {
			if ((w->work_type == WORK_TEST ||
			     w->work_type == WORK_DBLCHK ||
			     w->work_type == WORK_PRP) && filename[0] == 'p') {
				filename[0] = 'm';
				continue;
			}
			if ((w->work_type == WORK_TEST ||
			     w->work_type == WORK_DBLCHK) && filename[0] == 'm') {
				filename[0] = 'f';
				continue;
			}
			break;
		}

/* Read the header */

		res = read_header (fd, &version, w, NULL);
		_close (fd);
		if (!res) break;

/* We've successfully read a save file header, return now that the work */
/* unit's stage and pct_complete fields have been filled in. */

		return;
	}

/* We have not started working on this item */

	w->stage[0] = 0;
	w->pct_complete = 0.0;
}

/* Add a known Fermat factor to the known_factors list */

void addKnownFermatFactor (
	struct work_unit *w,	/* Work unit to modify */
	const char *factor)	/* Known Fermat factor */
{
	if (w->known_factors == NULL) {
		w->known_factors = (char *) malloc (strlen (factor) + 1);
		if (w->known_factors != NULL) {
			strcpy (w->known_factors, factor);
			WORKTODO_CHANGED = TRUE;
		}
	}
	else if (strstr (w->known_factors, factor) == NULL) {
		char	*new_factor_list;
		new_factor_list = (char *) malloc (strlen (w->known_factors) + strlen (factor) + 2);
		if (new_factor_list != NULL) {
			sprintf (new_factor_list, "%s,%s", w->known_factors, factor);
			free (w->known_factors);
			w->known_factors = new_factor_list;
			WORKTODO_CHANGED = TRUE;
		}
	}
}

/* Add all known Fermat factors to the known_factors list */

void addKnownFermatFactors (
	struct work_unit *w)	/* Work unit to modify */
{

/* If this is ECM, P-1, or P+1 on a Fermat number, then automatically add known Fermat factors */

	if (w->k == 1.0 && w->b == 2 && w->c == 1 &&
	    (w->work_type == WORK_ECM || w->work_type == WORK_PMINUS1 || w->work_type == WORK_PPLUS1) &&
	    IniGetInt (INI_FILE, "AddKnownFermatFactors", 1)) {
		if (w->n == 4096) addKnownFermatFactor (w, "114689");
		if (w->n == 4096) addKnownFermatFactor (w, "26017793");
		if (w->n == 4096) addKnownFermatFactor (w, "63766529");
		if (w->n == 4096) addKnownFermatFactor (w, "190274191361");
		if (w->n == 4096) addKnownFermatFactor (w, "1256132134125569");
		if (w->n == 4096) addKnownFermatFactor (w, "568630647535356955169033410940867804839360742060818433");
		if (w->n == 8192) addKnownFermatFactor (w, "2710954639361");
		if (w->n == 8192) addKnownFermatFactor (w, "2663848877152141313");
		if (w->n == 8192) addKnownFermatFactor (w, "3603109844542291969");
		if (w->n == 8192) addKnownFermatFactor (w, "319546020820551643220672513");
		if (w->n == 16384) addKnownFermatFactor (w, "116928085873074369829035993834596371340386703423373313");
		if (w->n == 32768) addKnownFermatFactor (w, "1214251009");
		if (w->n == 32768) addKnownFermatFactor (w, "2327042503868417");
		if (w->n == 32768) addKnownFermatFactor (w, "168768817029516972383024127016961");
		if (w->n == 65536) addKnownFermatFactor (w, "825753601");
		if (w->n == 65536) addKnownFermatFactor (w, "188981757975021318420037633");
		if (w->n == 131072) addKnownFermatFactor (w, "31065037602817");
		if (w->n == 131072) addKnownFermatFactor (w, "7751061099802522589358967058392886922693580423169");
		if (w->n == 262144) addKnownFermatFactor (w, "13631489");
		if (w->n == 262144) addKnownFermatFactor (w, "81274690703860512587777");
		if (w->n == 524288) addKnownFermatFactor (w, "70525124609");
		if (w->n == 524288) addKnownFermatFactor (w, "646730219521");
		if (w->n == 524288) addKnownFermatFactor (w, "37590055514133754286524446080499713");
		if (w->n == 2097152) addKnownFermatFactor (w, "4485296422913");
		if (w->n == 4194304) addKnownFermatFactor (w, "64658705994591851009055774868504577");
		if (w->n == 8388608) addKnownFermatFactor (w, "167772161");
		if (w->n == 33554432) addKnownFermatFactor (w, "25991531462657");
		if (w->n == 33554432) addKnownFermatFactor (w, "204393464266227713");
		if (w->n == 33554432) addKnownFermatFactor (w, "2170072644496392193");
		if (w->n == 67108864) addKnownFermatFactor (w, "76861124116481");
		if (w->n == 134217728) addKnownFermatFactor (w, "151413703311361");
		if (w->n == 134217728) addKnownFermatFactor (w, "231292694251438081");
		if (w->n == 268435456) addKnownFermatFactor (w, "1766730974551267606529");
		if (w->n == 536870912) addKnownFermatFactor (w, "2405286912458753");
	}
}	    

/* Add a work_unit to the work_unit array.  Grow the work_unit array if necessary */

int addToWorkUnitArray (
	unsigned int tnum,	/* Thread number that will run work unit */
	struct work_unit *w,	/* Work unit to add to array */
	int	add_point)	/* Three possible insertion points */
{
	struct work_unit *insertion_point;

/* When there are more worker sections than workers (as can happen when the user shrinks the */
/* number of worker windows), redistribute lines from those worker sections to the active sections. */

	tnum = tnum % NUM_WORKERS;

/* If add_to_end is set, then we are reading the worktodo.txt file.  Add the entry to the end of the array. */

	switch (add_point) {
	case ADD_TO_END:
		insertion_point = WORK_UNITS[tnum].last;
		break;

/* If add_to_front is set, add work unit to the front of the array after any comment lines. */

	case ADD_TO_FRONT:
		if (WORK_UNITS[tnum].first != NULL && WORK_UNITS[tnum].first->work_type == WORK_NONE)
			for (insertion_point = WORK_UNITS[tnum].first; insertion_point->next != NULL; insertion_point = insertion_point->next) {
				if (insertion_point->next->work_type != WORK_NONE) break;
			}
		else
			insertion_point = NULL;
		break;

/* If add_to_end, add work unit before the last blank line */

	case ADD_TO_LOGICAL_END:
		for (insertion_point = WORK_UNITS[tnum].last; insertion_point != NULL; insertion_point = insertion_point->prev) {
			if (insertion_point->work_type != WORK_NONE || insertion_point->comment[0]) break;
		}
		break;

/* Otherwise, add work unit before or after a specified work unit */

	case ADD_BEFORE_SPECIFIC:
		insertion_point = w->next->prev;
		break;
	case ADD_AFTER_SPECIFIC:
		insertion_point = w->next;
		break;
	}

/* Now insert the work unit after the insertion point */

	w->prev = insertion_point;
	if (insertion_point == NULL) {
		w->next = WORK_UNITS[tnum].first;
		WORK_UNITS[tnum].first = w;
	} else {
		w->next = insertion_point->next;
		insertion_point->next = w;
	}
	if (w->next == NULL)
		WORK_UNITS[tnum].last = w;
	else
		w->next->prev = w;

/* Bump count of valid work lines and wake up thread if it is waiting for work to do. */

	if (w->work_type != WORK_NONE) {
		WORKTODO_COUNT++;
		restart_one_waiting_worker (tnum, RESTART_WORK_AVAILABLE);
	}

/* Set flag indicating worktodo needs writing */

	WORKTODO_CHANGED = TRUE;

/* Return success */

	return (0);
}

/* Parse one worktodo.txt line */

int parseWorkToDoLine (
	char	*line,
	struct work_unit *w)
{
	char	keyword[20], buf[200];
	char	*value;
	int	i;

/* All lines other than keyword=value are saved as comment lines. */

	memset (w, 0, sizeof (struct work_unit));
	if (((line[0] < 'A' || line[0] > 'Z') && (line[0] < 'a' || line[0] > 'z'))) goto comment;

/* Otherwise, parse keyword=value lines */

	for (i = 0; ; i++) {
		if (line[i] == '=') break;
		if (line[i] == 0 || i == sizeof (keyword) - 1) goto illegal_line;
		keyword[i] = toupper (line[i]);
	}
	keyword[i] = 0;
	value = line + i + 1;

/* Set some default values.  Historically, this program worked on Mersenne numbers only. */

	w->k = 1.0;
	w->b = 2;
	w->c = -1;

/* Parse the optional assignment_uid */

	if (toupper (value[0]) == 'N' && value[1] == '/' && toupper (value[2]) == 'A' && value[3] == ',') {
		w->ra_failed = TRUE;
		safe_strcpy (value, value+4);
	}
	for (i = 0; ; i++) {
		if (!(value[i] >= '0' && value[i] <= '9') && !(toupper (value[i]) >= 'A' && toupper (value[i]) <= 'F')) break;
		if (i == 31) {
			if (value[32] != ',') break;
			value[32] = 0;
			strcpy (w->assignment_uid, value);
			safe_strcpy (value, value+33);
			break;
		}
	}

/* Parse the FFT length to use.  The syntax is FFT_length for x87 cpus and FFT2_length for SSE2 machines.  We support two syntaxes so that an */
/* assignment moved from an x87 to-or-from an SSE2 machine will recalculate the soft FFT crossover. */

	if (toupper (value[0]) == 'F' && toupper (value[1]) == 'F' && toupper (value[2]) == 'T') {
		int	sse2;
		unsigned long fftlen;
		char	*p;

		if (value[3] == '2') {
			sse2 = TRUE;
			p = value+5;
		} else {
			sse2 = FALSE;
			p = value+4;
		}
		fftlen = atoi (p);
		while (isdigit (*p)) p++;
		if (toupper (*p) == 'K') fftlen <<= 10, p++;
		if (toupper (*p) == 'M') fftlen <<= 20, p++;
		if (*p == ',') p++;
		safe_strcpy (value, p);
		if ((sse2 && (CPU_FLAGS & CPU_SSE2)) || (!sse2 && ! (CPU_FLAGS & CPU_SSE2)))
			w->minimum_fftlen = fftlen;
	}

/* Parse the optional file extension to use on save files (no good use right now, was formerly used for multiple workers ECMing the same number) */

	if ((value[0] == 'E' || value[0] == 'e') && (value[1] == 'X' || value[1] == 'x') && (value[2] == 'T' || value[2] == 't') && value[3] == '=') {
		char	*comma, *p;

		p = value+4;
		comma = strchr (p, ',');
		if (comma != NULL) {
			*comma = 0;
			if (strlen (p) > 8) p[8] = 0;
			strcpy (w->extension, p);
			safe_strcpy (value, comma+1);
		}
	}

/* Handle Test= and DoubleCheck= lines.					*/
/*	Test=exponent,how_far_factored,has_been_pminus1ed		*/
/*	DoubleCheck=exponent,how_far_factored,has_been_pminus1ed	*/
/* New in 30.4, for consistency with PRP worktodo lines assume no TF or P-1 needed if those fields are left out */

	if (strcmp (keyword, "TEST") == 0) {
		float	sieve_depth;
		w->work_type = WORK_TEST;
		sieve_depth = 99.0;
		w->pminus1ed = 1;
		sscanf (value, "%lu,%f,%d", &w->n, &sieve_depth, &w->pminus1ed);
		w->sieve_depth = sieve_depth;
		w->tests_saved = 1.3;
	}
	else if (strcmp (keyword, "DOUBLECHECK") == 0) {
		float	sieve_depth;
		w->work_type = WORK_DBLCHK;
		sieve_depth = 99.0;
		w->pminus1ed = 1;
		sscanf (value, "%lu,%f,%d", &w->n, &sieve_depth, &w->pminus1ed);
		w->sieve_depth = sieve_depth;
		w->tests_saved = 1.0;
	}

/* Handle AdvancedTest= lines. */
/*	AdvancedTest=exponent */

	else if (strcmp (keyword, "ADVANCEDTEST") == 0) {
		w->work_type = WORK_ADVANCEDTEST;
		sscanf (value, "%lu", &w->n);
	}

/* Handle Factor= lines.  Old style is:					*/
/*	Factor=exponent,how_far_factored				*/
/* New style is:							*/
/*	Factor=exponent,how_far_factored,how_far_to_factor_to		*/

	else if (strcmp (keyword, "FACTOR") == 0) {
		float	sieve_depth, factor_to;
		w->work_type = WORK_FACTOR;
		sieve_depth = 0.0;
		factor_to = 0.0;
		sscanf (value, "%lu,%f,%f", &w->n, &sieve_depth, &factor_to);
		w->sieve_depth = sieve_depth;
		w->factor_to = factor_to;
	}

/* Handle Pfactor= lines.  Old style is:						*/
/*	Pfactor=exponent,how_far_factored,double_check_flag				*/
/* New style is:									*/
/*	Pfactor=k,b,n,c,how_far_factored,ll_tests_saved_if_factor_found			*/
/* Starting in 30.9:									*/
/*	Pfactor=k,b,n,c,how_far_factored,ll_tests_saved_if_factor_found[,known_factors]	*/

	else if (strcmp (keyword, "PFACTOR") == 0) {
		float	sieve_depth;
		w->work_type = WORK_PFACTOR;
		sieve_depth = 0.0;
		if (countCommas (value) > 3) {		/* New style */
			char	*q;
			float	tests_saved;
			tests_saved = 0.0;
			w->k = atof (value);
			if ((q = strchr (value, ',')) == NULL) goto illegal_line;
			sscanf (q+1, "%lu,%lu,%ld,%f,%f", &w->b, &w->n, &w->c, &sieve_depth, &tests_saved);
			for (i = 1; i <= 4; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
			q = strchr (q+1, ',');
			w->sieve_depth = sieve_depth;
			w->tests_saved = tests_saved;
			if (q != NULL && q[1] == '"') {
				w->known_factors = (char *) malloc (strlen (q));
				if (w->known_factors == NULL) goto nomem;
				strcpy (w->known_factors, q+2);
			}
		} else {				/* Old style */
			int	dblchk;
			sscanf (value, "%lu,%f,%d", &w->n, &sieve_depth, &dblchk);
			w->sieve_depth = sieve_depth;
			w->tests_saved = dblchk ? 1.0 : 1.3;
		}
	}

/* Handle ECM= and ECM2= lines.  Note we've deprecated the old (2002ish) ECM= syntax and now treat ECM= and ECM2= the same. */
/*	ECM=k,b,n,c,B1[,B2-or-zero][,curves_to_do][,specific_sigma][,"factors"] */
/* Handle ECMSTAGE2= and ECMSTAGE2N= lines.  Used to continue stage 2 from a GMP-ECM stage 1 run.  Double quote file name if it contains special characters. */
/*	ECMSTAGE2=k,b,n,c,filename[,B2-or-zero][,skip_curves][,num_curves][,"known-factors"] */
/*	ECMSTAGE2N=filename[,B2-or-zero][,skip_curves][,num_curves][,"known-factors"] */

	else if (keyword[0] == 'E' && keyword[1] == 'C' && keyword[2] == 'M') {
		int	ecm_type;
		char	*q;
		w->work_type = WORK_ECM;
		if (strcmp (keyword, "ECM") == 0 || strcmp (keyword, "ECM2") == 0) ecm_type = 0;
		else if (strcmp (keyword, "ECMSTAGE2") == 0) ecm_type = 1;
		else if (strcmp (keyword, "ECMSTAGE2N") == 0) ecm_type = 2;
		else goto illegal_line;
		if (ecm_type != 2) {					// Get required k,b,n,c for ECM= and ECMSTAGE2=
			w->k = atof (value);
			if ((q = strchr (value, ',')) == NULL) goto illegal_line;
			sscanf (q+1, "%lu,%lu,%ld", &w->b, &w->n, &w->c);
			for (i = 1; i <= 3; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		}
		if (ecm_type != 0) {					// Get required filename for ECMSTAGE2= and ECMSTAGE2N=
			int len;
			char *end;
			if (ecm_type == 2) q = value; else q++;
			if (q[0] == '"') {
				q++;
				end = strchr (q, '"');
				if (end == NULL) goto illegal_line;
			} else {
				end = strchr (q, ',');
				if (end == NULL) end = q + strlen (q);
			}
			len = (int) (end - q);
			w->gmp_ecm_file = (char *) malloc (len + 1);
			if (w->gmp_ecm_file == NULL) goto nomem;
			memcpy (w->gmp_ecm_file, q, len);
			w->gmp_ecm_file[len] = 0;
			q = strchr (q + len, ',');
		}
		if (ecm_type == 0) {					// Get required B1 for ECM=
			w->B1 = (uint64_t) atof (q+1);
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] != '"') {				// Get optional B2 for all ECM types
			w->B2 = (uint64_t) atof (q+1);
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] != '"' && ecm_type != 0) {	// Get optional first curve (line number) for ECMSTAGE2= and ECMSTAGE2N=
			w->skip_curves = (uint64_t) atoi (q+1);
			q = strchr (q+1, ',');
		}
		if (ecm_type == 0) w->curves_to_do = 100;		// Default to 100 curves for ECM=
		if (q != NULL && q[1] != '"') {				// Get optional num_curves (num lines) for all ECM types
			w->curves_to_do = atoi (q+1);
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] != '"' && ecm_type == 0) {	// Get optional sigma for ECM=
			w->curve = atoll (q+1);
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] == '"') {				// Get optional known factors for all ECM types
			w->known_factors = (char *) malloc (strlen (q));
			if (w->known_factors == NULL) goto nomem;
			strcpy (w->known_factors, q+2);
		}
	}

/* Handle Pminus1 lines:						*/
/*	Pminus1=k,b,n,c,B1,B2[,how_far_factored][,B2_start][,"factors"] */

	else if (strcmp (keyword, "PMINUS1") == 0) {
		char	*q;
		w->work_type = WORK_PMINUS1;
		w->k = atof (value);
		if ((q = strchr (value, ',')) == NULL) goto illegal_line;
		sscanf (q+1, "%lu,%lu,%ld", &w->b, &w->n, &w->c);
		for (i = 1; i <= 3; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		w->B1 = (uint64_t) atof (q+1);
		if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		w->B2 = (uint64_t) atof (q+1);
		q = strchr (q+1, ',');
		w->sieve_depth = 0.0;
		if (q != NULL && q[1] != '"') {
			double j = atof (q+1);
			if (j <= 300.0) w->sieve_depth = j;
			q = strchr (q+1, ',');
		}
		w->B2_start = 0;
		if (q != NULL && q[1] != '"') {
			uint64_t j;
			j = (uint64_t) atof (q+1);
			if (j > w->B1) w->B2_start = j;
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] == '"') {
			w->known_factors = (char *) malloc (strlen (q));
			if (w->known_factors == NULL) goto nomem;
			strcpy (w->known_factors, q+2);
		}
	}

/* Handle Pplus1 lines:							*/
/*	Pplus1=k,b,n,c,B1,B2,nth_run[,how_far_factored][,"factors"]	*/
/* where nth_run is 1 for start 2/7, 2 for start 6/5, 3+ for random start */

	else if (strcmp (keyword, "PPLUS1") == 0) {
		char	*q;
		w->work_type = WORK_PPLUS1;
		w->k = atof (value);
		if ((q = strchr (value, ',')) == NULL) goto illegal_line;
		sscanf (q+1, "%lu,%lu,%ld", &w->b, &w->n, &w->c);
		for (i = 1; i <= 3; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		w->B1 = (uint64_t) atof (q+1);
		if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		w->B2 = (uint64_t) atof (q+1);
		q = strchr (q+1, ',');
		w->nth_run = 1;
		if (q != NULL && q[1] != '"') {
			int nth_run = atoi (q+1);
			if (nth_run >= 1 && nth_run <= 3) w->nth_run = nth_run;
			q = strchr (q+1, ',');
		}
		w->sieve_depth = 0.0;
		if (q != NULL && q[1] != '"') {
			double j = atof (q+1);
			if (j <= 300.0) w->sieve_depth = j;
			q = strchr (q+1, ',');
		}
		if (q != NULL && q[1] == '"') {
			w->known_factors = (char *) malloc (strlen (q));
			if (w->known_factors == NULL) goto nomem;
			strcpy (w->known_factors, q+2);
		}
	}

/* Handle PRP= lines.									*/
/*	PRP=k,b,n,c[,how_far_factored,tests_saved[,base,residue_type]][,known_factors]	*/
/*	PRPDC=k,b,n,c[,how_far_factored,tests_saved[,base,residue_type]][,known_factors]*/
/* A tests_saved value of 0.0 will bypass any P-1 factoring				*/
/* The PRP residue type is defined in primenet.h					*/

	else if (strcmp (keyword, "PRP") == 0 || strcmp (keyword, "PRPDC") == 0) {
		char	*q;

		w->work_type = WORK_PRP;
		w->prp_dblchk = (keyword[3] != 0);
		w->k = atof (value);
		if ((q = strchr (value, ',')) == NULL) goto illegal_line;
		sscanf (q+1, "%lu,%lu,%ld", &w->b, &w->n, &w->c);
		for (i = 1; i <= 2; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
		q = strchr (q+1, ',');

		w->sieve_depth = 99.0;		// Default to "no TF needed"
		w->tests_saved = 0.0;		// Default to "no P-1 needed"
		w->prp_base = 0;
		w->prp_residue_type = 0;
		if (q != NULL && q[1] != '"') {
			w->sieve_depth = atof (q+1);
			if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
			w->tests_saved = atof (q+1);
			q = strchr (q+1, ',');
			if (q != NULL && q[1] != '"') {
				w->prp_base = atoi (q+1);
				if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
				w->prp_residue_type = atoi (q+1);
				q = strchr (q+1, ',');
			}
		}
		if (q != NULL && q[1] == '"') {
			w->known_factors = (char *) malloc (strlen (q));
			if (w->known_factors == NULL) goto nomem;
			strcpy (w->known_factors, q+2);
		}
	}

/* Handle Cert= lines.  Certifying a PRP proof.		*/
/*	Cert=k,b,n,c,num_squarings			*/

	else if (strcmp (keyword, "CERT") == 0) {
		char	*q;

		w->work_type = WORK_CERT;
		w->k = atof (value);
		if ((q = strchr (value, ',')) == NULL) goto illegal_line;
		sscanf (q+1, "%lu,%lu,%ld,%d", &w->b, &w->n, &w->c, &w->cert_squarings);
		for (i = 1; i <= 3; i++) if ((q = strchr (q+1, ',')) == NULL) goto illegal_line;
	}

/* Uh oh.  We have a worktodo.txt line we cannot process. */

	else if (strcmp (keyword, "ADVANCEDFACTOR") == 0) {
		OutputSomewhere (MAIN_THREAD_NUM, "Worktodo error: AdvancedFactor no longer supported\n");
		goto comment;
	} else {
		goto illegal_line;
	}

/* Trim trailing non-digit characters from known factors list (this should be the closing double quote) */
/* Turn all non-digit characters into commas (they should be anyway) */

	if (w->known_factors != NULL) {
		for (i = (int) strlen (w->known_factors); i > 0 && !isdigit (w->known_factors[i-1]); i--);
		w->known_factors[i] = 0;
		for (i = 0; i < (int) strlen (w->known_factors); i++)
			if (!isdigit (w->known_factors[i])) w->known_factors[i] = ',';
	}

/* If this is ECM or P-1 on a Fermat number, then automatically add known Fermat factors */

	addKnownFermatFactors (w);

/* Make sure this line of work from the file makes sense. The exponent should be a prime number, bounded by values we can handle, and we */
/* should never be asked to factor a number more than we are capable of. */

	if (w->k == 1.0 && w->b == 2 && !isPrime (w->n) && w->c == -1 && w->known_factors == NULL &&
	    w->work_type != WORK_ECM && w->work_type != WORK_PMINUS1 && w->work_type != WORK_PPLUS1 &&
	    !(w->work_type == WORK_PRP && IniGetInt (INI_FILE, "PhiExtensions", 0))) {
		sprintf (buf, "Error: Worktodo.txt file contained composite exponent: %ld\n", w->n);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}
	if ((w->work_type == WORK_TEST || w->work_type == WORK_DBLCHK || w->work_type == WORK_ADVANCEDTEST) &&
	    (w->n < MIN_PRIME ||
	     (w->minimum_fftlen == 0 &&
	      w->n > (unsigned long) ((CPU_FLAGS & CPU_AVX512F) ? MAX_PRIME_AVX512 :
				      (CPU_FLAGS & CPU_FMA3) ? MAX_PRIME_FMA3 :
				      (CPU_FLAGS & CPU_AVX) ? MAX_PRIME_AVX :
				      (CPU_FLAGS & CPU_SSE2) ? MAX_PRIME_SSE2 : MAX_PRIME)))) {
		sprintf (buf, "Error: Worktodo.txt file contained bad LL exponent: %ld\n", w->n);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}
	if (w->work_type == WORK_FACTOR && w->n < 200000) {
		sprintf (buf, "Error: Use ECM instead of trial factoring for exponent: %ld\n", w->n);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}
	if (w->work_type == WORK_FACTOR && w->n > MAX_FACTOR && !IniGetInt (INI_FILE, "LargeTFexponents", 0)) {
		sprintf (buf, "Error: Worktodo.txt file contained bad factoring exponent: %ld\n", w->n);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}
	if (w->work_type == WORK_FACTOR && w->factor_to > MAX_FACTOR_BITS && !IniGetInt (INI_FILE, "LargeTFbits", 0)) {
		sprintf (buf, "Error: Worktodo.txt file contained bad factoring limit: %d\n", (int) w->factor_to);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}
	if ((w->work_type == WORK_PMINUS1 || w->work_type == WORK_PPLUS1 || w->work_type == WORK_ECM) && w->B1 < 50000 &&
	    w->gmp_ecm_file == NULL && !IniGetInt (INI_FILE, "AllowLowB1", 0)) {
		sprintf (buf, "Error: Worktodo.txt file has P-1/P+1/ECM with B1 < 50000 (exponent: %ld)\n", w->n);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto illegal_line;
	}

/* A user discovered a case where a computer that dual boots between 32-bit prime95 and 64-bit prime95 can run into problems.  If near the FFT limit an FFT */
/* length is picked and written to worktodo.txt.  When running the other executable, that FFT length may not be supported leading to a "cannot initialize */
/* FFT error".  For example, the 2800K FFT length is implemented in 64-bit prime95, but not 32-bit prime95.  The quick workaround here is to ignore FFT */
/* lengths from the worktodo file if that FFT length is not supported.  This is non-optimal because the proper FFT size will have to be recalculated. */
//  This should not be necessary now that we use gwnum's minimum_fftlen
//	if (w->minimum_fftlen && gwmap_fftlen_to_max_exponent (w->minimum_fftlen) == 0) {
//		sprintf (buf, "Warning: Ignoring unsupported FFT length, %ld, on line %u of worktodo.txt.\n", w->minimum_fftlen, linenum);
//		OutputBoth (MAIN_THREAD_NUM, buf);
//		w->minimum_fftlen = 0;
//	}

/* Do more initialization of the work_unit structure */

	auxiliaryWorkUnitInit (w);

// Return success

	return (0);

// Handle error conditions

illegal_line:
	sprintf (buf, "Illegal line in worktodo.txt file: %.80s\n", line);
	OutputSomewhere (MAIN_THREAD_NUM, buf);

comment:w->work_type = WORK_NONE;
	w->comment = (char *) malloc (strlen (line) + 1);
	if (w->comment == NULL) goto nomem;
	strcpy (w->comment, line);
	return (0);

nomem:	return (OutOfMemory (MAIN_THREAD_NUM));
}

/* Read the entire worktodo.txt file into memory.  Return error_code if we have a memory or file I/O error. */

int readWorkToDoFile (void)
{
	FILE	*fd;
	unsigned int tnum, i, linenum;
	int	rc;
	char	line[16384];

/* Grab the lock so that comm thread cannot try to add work units while */
/* file is being read in. */

	for (i = 1; ; i++) {
		gwmutex_lock (&WORKTODO_MUTEX);

/* Make sure no other threads are accessing work units right now. */
/* There should be no workers active so any use should be short-lived. */

		if (WORKTODO_IN_USE_COUNT == 0 && !WORKTODO_CHANGED) break;
		gwmutex_unlock (&WORKTODO_MUTEX);
		if (i <= 10) {
			Sleep (50);
			continue;
		}

/* Uh oh, the lock hasn't been released after half-a-second.  This happens processing large */
/* worktodo.txt files in communicateWithServer (see James Heinrich's complaints in 26.4 thread). */
/* As a workaround, we'll simply not re-read the worktodo.txt file now.  We only reread the file */
/* to pick up any manual edits that may have taken place since the last time worktodo.txt was */
/* read in (and to process worktodo.add).  Hopefully the comm-with-server thread will finish up */
/* and we can successfully re-read the worktodo.txt file at a later time. */

		return (0);
	}

/* Clear file needs writing flag and count of worktodo lines */

	WORKTODO_CHANGED = FALSE;
	WORKTODO_COUNT = 0;

/* Free old work_units for each worker. */
/* We sometimes reread the worktodo.txt file in case the user */
/* manually edits the file while the program is running. */

	for (tnum = 0; tnum < MAX_NUM_WORKERS; tnum++) {
		struct work_unit *w, *next_w;
		for (w = WORK_UNITS[tnum].first; w != NULL; w = next_w) {
			next_w = w->next;
			free (w->gmp_ecm_file);
			free (w->known_factors);
			free (w->comment);
			free (w);
		}
		WORK_UNITS[tnum].first = NULL;
		WORK_UNITS[tnum].last = NULL;
	}

/* Read the lines of the work file.  It is OK if the worktodo.txt file does not exist. */

	fd = fopen (WORKTODO_FILE, "r");
	if (fd == NULL) goto done;

	tnum = 0;
	linenum = 0;
	while (fgets (line, sizeof (line), fd)) {
	    struct work_unit *w;

/* Remove trailing CRLFs */

	    if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
	    if (line[0] && line[strlen(line)-1] == '\r') line[strlen(line)-1] = 0;
	    linenum++;

/* Allocate a work unit structure */

	    w = (struct work_unit *) malloc (sizeof (struct work_unit));
	    if (w == NULL) goto nomem;

/* A section header preceeds each worker's work units.  The first section need not be preceeded by a section header. */

	    if (line[0] == '[' && linenum > 1) {
		tnum++;
		if (tnum >= NUM_WORKERS) {
		    char	buf[100];
		    sprintf (buf,
			     "Too many sections in worktodo.txt.  Moving work from section #%u to #%u.\n",
			     tnum + 1, tnum % NUM_WORKERS + 1);
		    OutputSomewhere (MAIN_THREAD_NUM, buf);
		    safe_strcpy (line + 9, line);
		    memcpy (line, ";;MOVED;;", 9);
		    WORKTODO_CHANGED = TRUE;
		}
	    }

/* Parse the worktodo line */

	    rc = parseWorkToDoLine (line, w);
	    if (rc) goto retrc;

/* Grow the work_unit array if necessary and add this entry */

	    rc = addToWorkUnitArray (tnum, w, ADD_TO_END);
	    if (rc) goto retrc;
	}

/* Now that we've finished reading the worktodo file, set stage and pct_complete based on existing save files. */

	for (tnum = 0; tnum < MAX_NUM_WORKERS; tnum++) {
	    struct work_unit *w;
	    int first_real_work_line;

	    first_real_work_line = TRUE;
	    for (w = WORK_UNITS[tnum].first; w != NULL; w = w->next) {

/* Init assuming we won't find a save file. */

		w->stage[0] = 0;
		w->pct_complete = 0.0;

/* Skip comment lines */

		if (w->work_type == WORK_NONE) continue;

/* If well behaved work is set, only first work lines can have a save file */

		if (WELL_BEHAVED_WORK && !first_real_work_line) goto next_wu;

/* Set stage and pct_complete for work units that have already begun */
/* based on data in the save files.  Only do this for the first appearance */
/* of a number for a worker.  For example, if a worker has several entries */
/* ECMing the same number, only the first entry will have the pct_complete set. */
/* We also assume an existing save file is used for the first worker of a thread */
/* rather than a non-first work unit in an earlier thread. */

		if (!first_real_work_line) {
			int	tnum2;
			struct work_unit *w2;

/* See if any other worker's first work unit is testing the same number. */
/* If so, assume any existing save files are for that worker */

			for (tnum2 = 0; tnum2 < MAX_NUM_WORKERS; tnum2++) {
				for (w2 = WORK_UNITS[tnum2].first; w2 != NULL; w2 = w2->next) {
					if (w2->work_type == WORK_NONE) continue;
					if (w2->work_type == w->work_type &&
					    w2->k == w->k &&
					    w2->b == w->b &&
					    w2->n == w->n &&
					    w2->c == w->c) goto next_wu;
					break;
				}
			}

/* See if any earlier work units in this worker are testing the same number. */
/* If so, assume any existing save files are for that work unit. */

			for (w2 = WORK_UNITS[tnum].first; w2 != w; w2 = w2->next) {
				if (w2->work_type == w->work_type &&
				    w2->k == w->k &&
				    w2->b == w->b &&
				    w2->n == w->n &&
				    w2->c == w->c) goto next_wu;
			}
		}

/* Now see if an existing save file can be used to set stage and pct_complete */

		pct_complete_from_savefile (w);

/* Progress to the next work unit */

next_wu:	first_real_work_line = FALSE;
	    }
	}

/* Close the file, free the lock and return success */

	fclose (fd);
done:	gwmutex_unlock (&WORKTODO_MUTEX);

/* If the worktodo file changed, write the changed worktodo file */

	writeWorkToDoFile (FALSE);

/* Almost done.  Incorporate the optional worktodo.add file. */

	return (incorporateWorkToDoAddFile ());

/* Close the file, free the lock and return error from routine we called */

retrc:	fclose (fd);
	gwmutex_unlock (&WORKTODO_MUTEX);
	return (rc);

/* Free the lock and return out of memory error code */

nomem:	fclose (fd);
	gwmutex_unlock (&WORKTODO_MUTEX);
	return (OutOfMemory (MAIN_THREAD_NUM));
}

/* Write the updated worktodo.txt to disk */

int writeWorkToDoFile (
	int	force)		/* Force writing file even if WELL_BEHAVED */
{
	int	fd, last_line_was_blank;
	unsigned int tnum;

/* If work to do hasn't changed, then don't write the file */

	if (!WORKTODO_CHANGED) return (0);

/* If the well-behaved-work-option is on, then only write the file every */
/* half hour.  The user should set this option when the worktodo file is */
/* long and the work units complete quickly.  This commonly happens when */
/* trial factoring a large number of exponents to a low limit. */

	if (WELL_BEHAVED_WORK && !force) {
		static time_t last_time_written = 0;
		time_t	current_time;
		time (&current_time);
		if (current_time < last_time_written + 1800) return (0);
		last_time_written = current_time;
	}

/* Grab the lock so that comm thread cannot try to add work units while */
/* file is being written. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Create the WORKTODO.TXT file */

	fd = _open (WORKTODO_FILE, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_TEXT, CREATE_FILE_ACCESS);
	if (fd < 0) {
		OutputBoth (MAIN_THREAD_NUM, "Error creating worktodo.txt file\n");
		gwmutex_unlock (&WORKTODO_MUTEX);
		return (STOP_FILE_IO_ERROR);
	}

/* Loop over all workers */

	last_line_was_blank = FALSE;
	for (tnum = 0; tnum < MAX_NUM_WORKERS; tnum++) {
	    struct work_unit *w;
	    unsigned int len;

/* If we've processed all the workers and there is nothing left to output, then we are done. */

	    if (tnum >= NUM_WORKERS && WORK_UNITS[tnum].first == NULL) break;

/* Output a standardized section header */

	    if (tnum || NUM_WORKERS > 0) {
		char	buf[40];
		if (tnum && !last_line_was_blank && _write (fd, "\n", 1) != 1)
			goto write_error;
		sprintf (buf, "[Worker #%d]\n", tnum+1);
		len = (unsigned int) strlen (buf);
		if (_write (fd, buf, len) != len) goto write_error;
		last_line_was_blank = FALSE;
	    }

/* Loop over each assignment for this worker */

	    for (w = WORK_UNITS[tnum].first; w != NULL; w = w->next) {
		char	idbuf[100];
		char	buf[4096];

/* Do not output deleted lines */

		if (w->work_type == WORK_DELETED) continue;

/* Do not output section header */

		if (w == WORK_UNITS[tnum].first &&
		    w->work_type == WORK_NONE &&
		    w->comment[0] == '[') continue;

/* Format the optional assignment id */

		idbuf[0] = 0;
		if (w->assignment_uid[0])
			sprintf (idbuf, "%s,", w->assignment_uid);
		else if (w->ra_failed)
			sprintf (idbuf, "%s,", "N/A");

/* Format the FFT length */

		if (w->minimum_fftlen) {
			strcat (idbuf, "FFT");
			if (CPU_FLAGS & CPU_SSE2) strcat (idbuf, "2");
			if ((w->minimum_fftlen & 0xFFFFF) == 0)
				sprintf (idbuf+strlen(idbuf), "=%luM,", w->minimum_fftlen >> 20);
			else if ((w->minimum_fftlen & 0x3FF) == 0)
				sprintf (idbuf+strlen(idbuf), "=%luK,", w->minimum_fftlen >> 10);
			else
				sprintf (idbuf+strlen(idbuf), "=%lu,", w->minimum_fftlen);
		}

/* Output the optional file name extension (no good use right now, */
/* was formerly used for multiple workers ECMing the same number) */

		if (w->extension[0]) {
			sprintf (idbuf+strlen(idbuf), "EXT=%s,", w->extension);
		}

/* Write out comment lines just as we read them in */
/* Format normal work unit lines */

		switch (w->work_type) {

		case WORK_NONE:
			strcpy (buf, w->comment);
			break;

		case WORK_TEST:
			if (w->sieve_depth != 99.0 || w->pminus1ed != 1)
				sprintf (buf, "Test=%s%lu,%.0f,%d", idbuf, w->n, w->sieve_depth, w->pminus1ed);
			else
				sprintf (buf, "Test=%s%lu", idbuf, w->n);
			break;

		case WORK_DBLCHK:
			if (w->sieve_depth != 99.0 || w->pminus1ed != 1)
				sprintf (buf, "DoubleCheck=%s%lu,%.0f,%d", idbuf, w->n, w->sieve_depth, w->pminus1ed);
			else
				sprintf (buf, "DoubleCheck=%s%lu", idbuf, w->n);
			break;

		case WORK_ADVANCEDTEST:
			sprintf (buf, "AdvancedTest=%lu", w->n);
			break;

		case WORK_FACTOR:
			sprintf (buf, "Factor=%s%ld,%.0f,%.0f", idbuf, w->n, w->sieve_depth, w->factor_to);
			break;

		case WORK_PFACTOR:
			sprintf (buf, "Pfactor=%s%.0f,%lu,%lu,%ld,%g,%g", idbuf, w->k, w->b, w->n, w->c, w->sieve_depth, w->tests_saved);
			if (w->known_factors != NULL) sprintf (buf + strlen (buf), ",\"%s\"", w->known_factors);
			break;

		case WORK_ECM:
			if (w->gmp_ecm_file != NULL) {
				if (w->n == 0) sprintf (buf, "ECMSTAGE2N=%s\"%s\"", idbuf, w->gmp_ecm_file);
				else sprintf (buf, "ECMSTAGE2=%s%.0f,%lu,%lu,%ld,\"%s\"", idbuf, w->k, w->b, w->n, w->c, w->gmp_ecm_file);
				if (w->B2 || w->skip_curves || w->curves_to_do) sprintf (buf + strlen (buf), ",%" PRIu64, w->B2);
				if (w->skip_curves || w->curves_to_do) sprintf (buf + strlen (buf), ",%u", w->skip_curves);
				if (w->curves_to_do) sprintf (buf + strlen (buf), ",%u", w->curves_to_do);
			} else {
				sprintf (buf, "ECM=%s%.0f,%lu,%lu,%ld,%" PRIu64, idbuf, w->k, w->b, w->n, w->c, w->B1);
				if (w->B2 || w->curves_to_do != 100 || w->curve) sprintf (buf + strlen (buf), ",%" PRIu64, w->B2);
				if (w->curves_to_do != 100 || w->curve) sprintf (buf + strlen (buf), ",%u", w->curves_to_do);
				if (w->curve) sprintf (buf + strlen (buf), ",%" PRIu64, w->curve);
			}
			if (w->known_factors != NULL) sprintf (buf + strlen (buf), ",\"%s\"", w->known_factors);
			break;

		case WORK_PMINUS1:
			sprintf (buf, "Pminus1=%s%.0f,%lu,%lu,%ld,%" PRIu64 ",%" PRIu64, idbuf, w->k, w->b, w->n, w->c, w->B1, w->B2);
			if (w->sieve_depth > 0.0) sprintf (buf + strlen (buf), ",%.0f", w->sieve_depth);
			if (w->B2_start > w->B1) sprintf (buf + strlen (buf), ",%" PRIu64, w->B2_start);
			if (w->known_factors != NULL) sprintf (buf + strlen (buf), ",\"%s\"", w->known_factors);
			break;

		case WORK_PPLUS1:
			sprintf (buf, "Pplus1=%s%.0f,%lu,%lu,%ld,%" PRIu64 ",%" PRIu64 ",%d", idbuf, w->k, w->b, w->n, w->c, w->B1, w->B2, w->nth_run);
			if (w->sieve_depth > 0.0) sprintf (buf + strlen (buf), ",%.0f", w->sieve_depth);
			if (w->known_factors != NULL) sprintf (buf + strlen (buf), ",\"%s\"", w->known_factors);
			break;

		case WORK_PRP:
			sprintf (buf, "PRP%s=%s%.0f,%lu,%lu,%ld", w->prp_dblchk ? "DC" : "", idbuf, w->k, w->b, w->n, w->c);
			if (w->sieve_depth != 99.0 || w->tests_saved > 0.0 || w->prp_base || w->prp_residue_type) {
				sprintf (buf + strlen (buf), ",%g,%g", w->sieve_depth, w->tests_saved);
				if (w->prp_base || w->prp_residue_type)
					sprintf (buf + strlen (buf), ",%u,%d", w->prp_base, w->prp_residue_type);
			}
			if (w->known_factors != NULL) sprintf (buf + strlen (buf), ",\"%s\"", w->known_factors);
			break;

		case WORK_CERT:
			sprintf (buf, "Cert=%s%.0f,%lu,%lu,%ld,%d", idbuf, w->k, w->b, w->n, w->c, w->cert_squarings);
			break;
		}

/* Write out the formatted line */

		strcat (buf, "\n");
		len = (unsigned int) strlen (buf);
		if (_write (fd, buf, len) != len) {
write_error:		OutputBoth (MAIN_THREAD_NUM, "Error writing worktodo.txt file\n");
			_close (fd);
			gwmutex_unlock (&WORKTODO_MUTEX);
			return (STOP_FILE_IO_ERROR);
		}
		last_line_was_blank = (len == 1);
	    }
	}

/* Close file, unlock, and return success */

	_close (fd);
	WORKTODO_CHANGED = FALSE;
	gwmutex_unlock (&WORKTODO_MUTEX);
	return (0);
}

/* Return a worktodo.txt entry for the given worker */

struct work_unit *getNextWorkToDoLine (
	int	thread_num,		/* Thread number starting from 0 */
	struct work_unit *w,		/* Current WorkToDo entry (or NULL) */
	int	usage)			/* Short vs. long term usage */
{
	struct work_unit *next;		/* Next WorkToDo entry (or NULL) */

	ASSERTG (thread_num < (int) NUM_WORKERS);

/* Grab the lock so that other threads do not add or delete lines */
/* while we are finding the next entry. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Get pointer to the first or next work unit */

	next = (w == NULL) ? WORK_UNITS[thread_num].first : w->next;
	while (next != NULL && next->work_type == WORK_DELETED)
		next = next->next;

/* Decrement the use count */

	if (w != NULL) {
		w->in_use_count--;
		WORKTODO_IN_USE_COUNT--;
		if (usage == LONG_TERM_USE) w->in_use_count &= ~0x80000000;

/* Free the work unit if it has been deleted and use count is now zero */

		if (w->work_type == WORK_DELETED && w->in_use_count == 0) {

/* Unlink the work unit from the list */

			if (w->prev == NULL)
				WORK_UNITS[thread_num].first = w->next;
			else
				w->prev->next = w->next;
			if (w->next == NULL)
				WORK_UNITS[thread_num].last = w->prev;
			else
				w->next->prev = w->prev;

/* Free memory allocated for this work unit */

			free (w->known_factors);
			free (w->comment);
			free (w);
		}
	}

/* Increment the in-use count.  If this is a long-term usage, remember */
/* the fact so that deleting the work unit can be prohibited. */

	if (next != NULL) {
		next->in_use_count++;
		WORKTODO_IN_USE_COUNT++;
		if (usage == LONG_TERM_USE) next->in_use_count |= 0x80000000;
	}

/* Unlock and return */

	gwmutex_unlock (&WORKTODO_MUTEX);
	return (next);
}

/* Return a worktodo.txt entry for the given worker */

void decrementWorkUnitUseCount (
	struct work_unit *w,		/* WorkToDo entry */
	int	usage)			/* Short vs. long term usage */
{
	ASSERTG (w->in_use_count != 0);

/* Grab the lock */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Decrement the reader count before getting the next entry */

	w->in_use_count--;
	WORKTODO_IN_USE_COUNT--;
	if (usage == LONG_TERM_USE) w->in_use_count &= ~0x80000000;

/* Unlock and return */

	gwmutex_unlock (&WORKTODO_MUTEX);
}

/* Add a line of work to the work-to-do INI file. */

int addWorkToDoLine (
	int	tnum,		/* Worker to process this work unit */
	struct work_unit *w,	/* Type of work */
	int	add_point)	/* Three possible insertion points */
{
	struct work_unit *malloc_w;
	int	rc;

/* Do more initialization of the work_unit structure */

	auxiliaryWorkUnitInit (w);

/* Copy work unit from stack to malloc'ed area */

	malloc_w = (struct work_unit *) malloc (sizeof (struct work_unit));
	if (malloc_w == NULL) return (OutOfMemory (MAIN_THREAD_NUM));
	memcpy (malloc_w, w, sizeof (struct work_unit));

/* If this is ECM, P-1, or P+1 on a Fermat number, then automatically add known Fermat factors */

	addKnownFermatFactors (malloc_w);

/* Grab the lock so that comm thread and/or workers do not */
/* access structure while the other is adding/deleting lines. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Add the work unit to the start or end of the array.  Well, actually before the last set of blank lines. */

	rc = addToWorkUnitArray (tnum, malloc_w, add_point);
	if (rc) goto retrc;

/* Unlock and write the worktodo.txt file to disk */

	gwmutex_unlock (&WORKTODO_MUTEX);
	return (writeWorkToDoFile (FALSE));

/* Unlock and return error code from called routine */

retrc:	gwmutex_unlock (&WORKTODO_MUTEX);
	return (rc);
}

/* Caller has updated a work unit structure such that the work-to-do INI file needs to be written. */

int updateWorkToDoLine (
	int	tnum,		/* Worker to process this work unit */
	struct work_unit *w)	/* Type of work */
{

/* Set flag indicating worktodo needs writing */

	WORKTODO_CHANGED = TRUE;

/* Write the worktodo.txt file to disk */

	return (writeWorkToDoFile (FALSE));
}

/* Delete a line of work from the work-to-do INI file.  Even if an error */
/* occurs the work unit has been deleted and the use_count decremented. */
/* The work_unit pointer (w) will be set to the previous work unit so that */
/* getNextWorkToDoLine works properly. */

int deleteWorkToDoLine (
	int	tnum,		/* Worker to process this work unit */
	struct work_unit *w,	/* Work unit pointer */
	int	stop_if_in_progress) /* Stop thread processing work unit */
{

/* If this work unit has already been deleted, then ignore this delete */
/* request.  This should only happen in a bizarre race condition. */

	if (w->work_type == WORK_DELETED) return (0);

/* Grab the lock so that comm thread and/or workers don't access structure while the other is adding/deleting lines. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* If we are deleting a work unit that is currently being worked on */
/* then set a flag so that the worker will abort the work unit */
/* at its first opportunity.  There is a race condition whereby the worker */
/* thread may move on to a different work unit before testing the */
/* abort-work-unit flag.  This is harmless as the new work unit will be */
/* aborted and then restarted. */

	if ((w->in_use_count & 0x80000000) && stop_if_in_progress)
		stop_worker_for_abort (tnum);

/* Decrement count of valid work lines */

	if (w->work_type != WORK_NONE) WORKTODO_COUNT--;

/* Mark this work unit deleted */

	w->work_type = WORK_DELETED;

/* Set flag indicating worktodo needs writing */

	WORKTODO_CHANGED = TRUE;

/* Unlock and write the worktodo.txt file to disk */

	gwmutex_unlock (&WORKTODO_MUTEX);
	return (writeWorkToDoFile (FALSE));
}

/* Return TRUE if a work unit is currently being worked on. */

int isWorkUnitActive (
	struct work_unit *w)	/* work_unit pointer */
{

/* At the present time only a worker actively working on a work unit */
/* sets the LONG_TERM_USE flag of in_use_count.  Use that fact, to decide */
/* if the work unit is active. */

	if (w->in_use_count & 0x80000000) return (TRUE);
	return (FALSE);
}

/* Make a guess as to how much longer a chore should take. */

double work_estimate (
	int	thread_num,		/* Thread number doing the work */
	struct work_unit *w)
{
	double	timing, est, pct_complete;
	int	can_use_multiple_threads;
	unsigned int i, total_cores;

/* I suppose there are race conditions where a deleted work unit could get here.  Return an estimate of 0.0. */

	est = 0.0;

/* Make sure the pct_complete is between 0.0 and 1.0.  There is presently */
/* a bug in P-1 code that is setting this value to more than 1.0 */

	pct_complete = w->pct_complete;
	if (pct_complete < 0.0) pct_complete = 0.0;
	if (pct_complete > 1.0) pct_complete = 1.0;

/* Only large FFTs can use multiple threads. */

	can_use_multiple_threads = (CPU_FLAGS & CPU_SSE2 && w->n > 172800) || (CPU_FLAGS & CPU_AVX && w->n > 130000) || (CPU_FLAGS & CPU_AVX512F && w->n > 172100);

/* For ECM, estimating time is very difficult because it depends on how */
/* much memory is available for temporaries in stage 2.  There are also */
/* significant increases in time when numbers no longer fit in the L2 */
/* or L3 cache.  */
/* For small numbers, we do about 12.86 * B1 squarings in stage 1 and */
/* 0.0617 * (B2 - B1) squarings in stage 2.  The extra overhead due to */
/* adds/subs and gwfftmul, gwfftfftmul being less efficient than gwsquare */
/* adds overhead, I'm guessing 10% for tiny numbers that likely fit in the */
/* cache to 20% for numbers that don't fit in the cache. */
/* For larger numbers, fewer temporaries and greater costs for modinv cause */
/* us to eventually switch from a 2 FFTs/prime to 4 FFTs/prime strategy in */
/* stage 2.  This switch occurs around 5,000,000 bits on a machine using */
/* modest amounts of memory.  We'll be doing 0.1261 * (B2 - B1) stage 2 */
/* squarings then.  Between 100,000 bits and 5,000,000 bits we'll gradually */
/* increase the stage 2 cost to account for the fewer temporaries resulting */
/* in more modular inverses combined with modular inverses getting more */
/* and more expensive. */
/* Also note: the stage text is C<curve#>S<stage#>. */

	if (w->work_type == WORK_ECM) {
		int	full_curves_to_do, stage, bits;
		double	stage1_time, stage2_time, overhead, B2_minus_B1;

		full_curves_to_do = w->curves_to_do;
		stage = 0;
		if (w->stage[0]) {
			full_curves_to_do -= atoi (&w->stage[1]);
			stage = atoi (&w->stage[strlen(w->stage)-1]);
		}

		timing = gwmap_to_timing (w->k, w->b, w->n, w->c);
		bits = (int) (w->n * log2 (w->b));
		if (bits <= 80000) overhead = 1.10;
		else if (bits >= 1500000) overhead = 1.20;
		else overhead = 1.10 + ((double) bits - 80000.0) / 1420000.0 * (1.20 - 1.10);

		stage1_time = (12.86 * w->B1) * timing * overhead;

		if (bits <= 100000) stage2_time = 0.0617;
		else if (bits >= 5000000) stage2_time = 0.1261;
		else stage2_time = 0.0617 + ((double) bits - 100000.0) / 4900000.0 * (0.1261 - 0.0617);
		B2_minus_B1 = (w->B2 > 0.0) ? w->B2 - w->B1 : 99.0 * w->B1;
		stage2_time = stage2_time * B2_minus_B1 * timing * overhead;

		est = (double) full_curves_to_do * (stage1_time + stage2_time);
		if (stage == 1)
			est += stage1_time * (1.0 - pct_complete) + stage2_time;
		if (stage == 2)
			est += stage2_time * (1.0 - pct_complete);

		est *= IniGetInt (INI_FILE, "ECMBoundsMultiplier", 1);

		if (w->gmp_ecm_file != NULL) est = 86400.0;
	}

/* For P-1, estimate about 1.4545 * B1 squarings in stage 1.  Estimate stage 2 will take half as long as stage 1.  The stage 2 estimate */
/* is a very rough guess but polymult stage 2 makes it extremely difficult to make an accurate estimate. */

	if (w->work_type == WORK_PMINUS1 || w->work_type == WORK_PFACTOR) {
		double	stage1_time, stage2_time;

		timing = gwmap_to_timing (w->k, w->b, w->n, w->c);

		if (w->work_type == WORK_PFACTOR) {
			// guess_pminus1_bounds is costly, especially if there are many pfactor assignments in worktodo.txt.  Since time estimates are
			// not essential and not very accurate anyways, assume pfactor time is 1.75% of a PRP test (from Skylake-X on 118M assignments).
			// uint64_t squarings;
			// double prob;
			// guess_pminus1_bounds (thread_num, w->k, w->b, w->n, w->c, w->sieve_depth, w->tests_saved, &B1, &B2, &squarings, &prob);
			stage1_time = 0.0117 * w->n * timing;
			stage2_time = 0.0058 * w->n * timing;
		} else {
			stage1_time = timing * (1.4545 * (double) w->B1);
			stage2_time = 0.5 * stage1_time;
		}

		int stage = w->stage[0] ? atoi (&w->stage[1]) : 0;
		if (stage == 0) est = stage1_time + stage2_time;
		if (stage == 1) est = stage1_time * (1.0 - pct_complete) + stage2_time;
		if (stage == 2) est = stage2_time * (1.0 - pct_complete);
	}

/* For P+1, stage 1 is 1.52 times P-1 stage 1, stage 2 is about the same */ 

	if (w->work_type == WORK_PPLUS1) {
		int	stage;
		uint64_t B1, B2;
		double	stage1_time, stage2_time;

		B1 = w->B1;
		B2 = w->B2 ? w->B2 : 40 * w->B1;

		if (w->stage[0]) stage = atoi (&w->stage[1]);
		else stage = 0;

		timing = gwmap_to_timing (w->k, w->b, w->n, w->c);
		stage1_time = timing * (1.4545 * 1.52 * (double) B1);
		stage2_time = (B2 >= B1) ? timing * (0.06154 * (double) (B2 - B1)) * 1.285 : 0.0;

		if (stage == 0) est = stage1_time + stage2_time;
		if (stage == 1) est = stage1_time * (1.0 - pct_complete) + stage2_time;
		if (stage == 2) est = stage2_time * (1.0 - pct_complete);
	}

/* If factoring, guess how long that will take.  Timings are based on */
/* the factoring benchmark for my 2 GHz P4. */
/*	Best time for 60 bit trial factors: 15.123 ms. */
/*	Best time for 61 bit trial factors: 15.021 ms. */
/*	Best time for 62 bit trial factors: 15.080 ms. */
/*	Best time for 63 bit trial factors: 16.127 ms. */
/*	Best time for 64 bit trial factors: 16.143 ms. */
/*	Best time for 65 bit trial factors: 20.230 ms. */
/*	Best time for 66 bit trial factors: 20.212 ms. */
/*	Best time for 67 bit trial factors: 20.244 ms. */
/* Factoring M35000011 from 2^60 to 2^61 takes 513 seconds.  Solve for */
/* constant C in this formula:  15.1 ms * 2^61 * C = 513 seconds */
/*	C = 513 sec / 2^61 / 15.1 ms */
/*	C = 513000 ms / 2^61 / 15.1 ms = 33974 / 2^61 */
/* Our estimate for factoring Mp to 2^i is then: */
/*	time_in_ms = benchmark_in_ms * 2^i * C * (35,000,011 / p) */
/* Which simplifies to: */
/*	time_in_seconds = benchmark_in_ms * 2^(i-48) * 145000 / p */

	if (w->work_type == WORK_FACTOR) {
		int	i, tf_level;

#ifdef X86_64
		can_use_multiple_threads = TRUE;
#else
		can_use_multiple_threads = FALSE;
#endif

		if (w->stage[0]) tf_level = atoi (&w->stage[2]);
		else tf_level = 0;

		est = 0.0;
		for (i = (int) w->sieve_depth+1; i <= (int) w->factor_to; i++) {
			if (i < 48) continue;
			timing = (i > 64) ? 20.2 : (i > 62) ? 16.1 : 15.1;
			if (i <= 72) timing *= 145000.0 * (1L << (i - 48)) / w->n;
			else timing *= 145000.0 * 16777216.0 * (1L << (i - 72)) / w->n;
			if (i == tf_level)
				est += timing * (1.0 - pct_complete);
			else
				est += timing;
		}
		est = est * 2000.0 / CPU_SPEED;

/* Core 2 and Core I7 CPUs are faster than the Pentium 4 I benchmarked.  I presume all later Intel CPUs are as well. */
/* AMD CPUs are faster than the Pentium 4 as well.  From the CPU benchmarks page, we find: */
/* Our original P4, TF to 65 bits -- 2.0 GHz P4 = 20.2 ms */
/* A 2.8 GHz Core 2 is 5.8 ms, which is 8.1 ms adjusting for different clock rates, or 2.5 times faster */
/* A 3.3 GHz i7 980 is 4.0 ms, which is 6.6 ms adjusting for different clock rates, or 3.1 times faster */
/* A 3.2 GHz Phenom 840 is 4.2 ms, which is 6.7 ms adjusting for different clock rates, or 3.0 times faster */

		if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_CORE_2) est /= 2.5;
		if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_CORE_I7 ||
		    CPU_ARCHITECTURE == CPU_ARCHITECTURE_PHI ||
		    CPU_ARCHITECTURE == CPU_ARCHITECTURE_INTEL_OTHER) est /= 3.1;
		if (CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K8 ||
		    CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_K10 ||
		    CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_BULLDOZER ||
		    CPU_ARCHITECTURE == CPU_ARCHITECTURE_AMD_OTHER) est /= 3.0;

/* Factor in the algorithmic improvements since the Pentium 4 TF benchmark was taken */

		if (CPU_FLAGS & CPU_AVX512F) est /= 4.0;
		else if (CPU_FLAGS & CPU_AVX2) est /= 2.0;
	}

/* If testing add in the Lucas-Lehmer testing time */

	if (w->work_type == WORK_TEST ||
	    w->work_type == WORK_ADVANCEDTEST ||
	    w->work_type == WORK_DBLCHK) {
		est = w->n * gwmap_to_timing (w->k, w->b, w->n, w->c);
		if (w->stage[0] == 'L') est *= (1.0 - pct_complete);
	}

/* If PRPing add in the PRP testing time */

	if (w->work_type == WORK_PRP) {
		est = w->n * log ((double) w->b) / log (2.0) * gwmap_to_timing (w->k, w->b, w->n, w->c);
		if (w->stage[0] == 'P') est *= (1.0 - pct_complete);
	}

/* If PRPing add in the PRP testing time */

	if (w->work_type == WORK_CERT) {
		est = w->cert_squarings * gwmap_to_timing (w->k, w->b, w->n, w->c);
		if (w->stage[0] == 'C') est *= (1.0 - pct_complete);
	}

/* Factor in the hours per day the computer is running and the */
/* rolling average */

	est *= (24.0 / CPU_HOURS) * (1000.0 / ROLLING_AVERAGE);

/* If the worker uses multiple CPUs to execute the FFT, then adjust the */
/* time estimate.  As a rough estimate, assume the first additional CPU */
/* reduces the time by a factor of 1.9.  Also assume each additional CPU */
/* is less beneficial.  Trial factoring is an exception -- it scales quite nicely. */

	if (can_use_multiple_threads && CORES_PER_TEST[thread_num] > 1) {
		double	effective_num_cpus, cpu_value;
		effective_num_cpus = 0.0;
		cpu_value = 1.0;
		for (i = 0; i < CORES_PER_TEST[thread_num]; i++) {
			effective_num_cpus += cpu_value;
			cpu_value *= (w->work_type == WORK_FACTOR ? 1.0 : 0.9);
		}
		est = est / effective_num_cpus;
	}

/* If the user unwisely oversubscribed the CPU cores, then increase the time estimate */

	total_cores = 0;
	for (i = 0; i < NUM_WORKERS; i++) total_cores += CORES_PER_TEST[i];
	if (total_cores > HW_NUM_CORES) est *= (double) total_cores / (double) HW_NUM_CORES;

/* Return the total estimated time in seconds */

	return (est);
}


/* Determine how much we should factor (in bits) */

unsigned int factorLimit (
	struct work_unit *w)
{
	unsigned long p;
	unsigned int test;

/* If this is trial factoring work with a specified end point, then */
/* return that end_point. */

	if (w->factor_to != 0.0) return ((unsigned int) w->factor_to);

/* For LL tests, determine the optimal trial factoring end point. */
/* See commonc.h for how these breakeven points were calculated. */

	p = w->n;
	if (p > FAC82) test = 82;	/* Test all 82 bit factors */
	else if (p > FAC81) test = 81;	/* Test all 81 bit factors */
	else if (p > FAC80) test = 80;	/* Test all 80 bit factors */
	else if (p > FAC79) test = 79;	/* Test all 79 bit factors */
	else if (p > FAC78) test = 78;	/* Test all 78 bit factors */
	else if (p > FAC77) test = 77;	/* Test all 77 bit factors */
	else if (p > FAC76) test = 76;	/* Test all 76 bit factors */
	else if (p > FAC75) test = 75;	/* Test all 75 bit factors */
	else if (p > FAC74) test = 74;	/* Test all 74 bit factors */
	else if (p > FAC73) test = 73;	/* Test all 73 bit factors */
	else if (p > FAC72) test = 72;	/* Test all 72 bit factors */
	else if (p > FAC71) test = 71;	/* Test all 71 bit factors */
	else if (p > FAC70) test = 70;	/* Test all 70 bit factors */
	else if (p > FAC69) test = 69;	/* Test all 69 bit factors */
	else if (p > FAC68) test = 68;	/* Test all 68 bit factors */
	else if (p > FAC67) test = 67;	/* Test all 67 bit factors */
	else if (p > FAC66) test = 66;	/* Test all 66 bit factors */
	else if (p > FAC65) test = 65;	/* Test all 65 bit factors */
	else if (p > FAC64) test = 64;	/* Test all 64 bit factors */
	else if (p > FAC63) test = 63;	/* Test all 63 bit factors */
	else if (p > FAC62) test = 62;	/* Test all 62 bit factors */
	else if (p > FAC61) test = 61;	/* Test all 61 bit factors */
	else if (p > FAC60) test = 60;	/* Test all 60 bit factors */
	else if (p > FAC59) test = 59;	/* Test all 59 bit factors */
	else if (p > FAC58) test = 58;	/* Test all 58 bit factors */
	else if (p > FAC57) test = 57;	/* Test all 57 bit factors */
	else if (p > FAC56) test = 56;	/* Test all 56 bit factors */
	else test = 40;			/* Test all 40 bit factors */

/* If double-checking, then trial factor to one less bit depth because */
/* a found factor will only save one LL test, not two. */

	if (w->work_type == WORK_DBLCHK) test--;

/* Return the computed end point. */

	return (test);
}

/**************************************************************/
/*            Routines to compute the rolling average         */
/**************************************************************/

//bug - someway to avoid prime.txt updates (to disk) if well_behaved_work is set!!

/* Convert a string to a hash value */

unsigned long string_to_hash (
	const char *str)		/* String to hash */
{
	char	md5val[33];
	int	i, j;
	char	*p;
	unsigned long hash, val;

/* Use md5 to generate a number to hash */

	md5_hexdigest_string (md5val, str);
	for (i = 0, p = md5val, hash = 0; i < 4; i++) {
		for (j = 0, val = 0; j < 8; j++, p++) {
			val = (val << 4) +
			      (*p < 'A' ? *p - '0' : *p - 'A' + 10);
		}
		hash += val;
	}
	return (hash & 0x7FFFFFFF);
}

/* Build the hash value as we go */

unsigned long build_rolling_hash (
	struct work_unit *w)		/* Work unit to add in to hash */
{
	char	buf[80];

/* Use md5 on a character rep of the number */

	gw_as_string (buf, w->k, w->b, w->n, w->c);
	return (string_to_hash (buf));
}

/* Adjust rolling average computation variables when a work unit completes */

void rolling_average_work_unit_complete (
	int	thread_num,		/* Thread number that completed work */
	struct work_unit *completed_w)
{
	unsigned long hash, time_to_complete;
	struct work_unit *first_w, *second_w;

/* Get current rolling average computation variables */

	hash = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingHash, 0);
	time_to_complete = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingCompleteTime, 0);

/* Grab the lock so that other threads do not add or delete lines */
/* while we are making this calculation. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Get first and second work unit pointers */

	for (first_w = WORK_UNITS[thread_num].first;
	     first_w != NULL && first_w->work_type == WORK_NONE;
	     first_w = first_w->next);
	for (second_w = first_w->next;
	     second_w != NULL && second_w->work_type == WORK_NONE;
	     second_w = second_w->next);

/* Only modify the values if we completed the first work_unit and there */
/* is a second work_unit */

	if (first_w == completed_w && second_w != NULL) {

/* Adjust the hash value.  Subtract out the completed work unit and add in */
/* the next work unit. */

		hash -= build_rolling_hash (first_w);
		hash += build_rolling_hash (second_w);

/* Adjust the estimated time to complete the first work unit */

		time_to_complete += (unsigned long)
			work_estimate (thread_num, second_w);
	}

/* Unlock access to the worktodo.txt structures */

	gwmutex_unlock (&WORKTODO_MUTEX);

/* Update the changed rolling average computation values */

	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingHash, hash);
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingCompleteTime, time_to_complete);
}

/* Adjust the rolling average */

void adjust_rolling_average (void)
{
	unsigned int tnum;
	unsigned long time_to_complete, starting_time_to_complete;
	time_t	current_time, starting_time;
	unsigned long hash, time_in_this_period;
	double	rolling_average_this_period, pct;

/* Grab the lock so that other threads do not add or delete lines */
/* while we are making this calculation. */

	gwmutex_lock (&WORKTODO_MUTEX);

/* Look at the first work unit in each thread */

	hash = 0;
	time_to_complete = 0;
	for (tnum = 0; tnum < NUM_WORKERS; tnum++) {
		struct work_unit *w;

		for (w = WORK_UNITS[tnum].first; w != NULL; w = w->next)
			if (w->work_type != WORK_NONE &&
			    w->work_type != WORK_DELETED) break;
		if (w == NULL) continue;

/* Build the hash value */

		hash += build_rolling_hash (w);

/* Get the new estimated time to complete this work unit */

		time_to_complete += (unsigned long) work_estimate (tnum, w);
	}

/* Unlock access to the worktodo.txt structures */

	gwmutex_unlock (&WORKTODO_MUTEX);

/* Get the current time and starting time */

	starting_time = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingStartTime, 0);
	time (&current_time);

/* Make sure hash codes match.  This protects us against making incorrect */
/* rolling average adjustments when user manually edits worktodo.txt. */

	if (hash != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingHash, 0)) goto no_update;

/* Now compute the rolling average for this time period.  For example, if */
/* the time-to-complete decreased 5 hours over the last 6 hours of elapsed */
/* time, then the rolling average for the last 6 hours was 5/6 of the */
/* current rolling average. */

	time_in_this_period = (unsigned long) (current_time - starting_time);
	starting_time_to_complete = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_RollingCompleteTime, 0);

	/* Sanity checks for bogus time periods and completion estimates*/

	if (starting_time == 0 ||
	    current_time <= starting_time ||
	    time_in_this_period > 30 * 86400 ||
	    starting_time_to_complete <= time_to_complete)
		goto no_update;

	rolling_average_this_period =
		(double) (starting_time_to_complete - time_to_complete) /
		(double) NUM_WORKERS /
		(double) time_in_this_period *
		(double) ROLLING_AVERAGE;

	/* If the user is running more workers than there are CPUs, then adjust the rolling average upward accordingly. */

	if (NUM_WORKERS > HW_NUM_CORES)
		rolling_average_this_period *= (double) NUM_WORKERS / (double) HW_NUM_CORES;

	/* More safeguard against bogus or abruptly changing data */

	if (rolling_average_this_period > 50000.0) goto no_update;
	if (rolling_average_this_period < 0.5 * ROLLING_AVERAGE) rolling_average_this_period = 0.5 * ROLLING_AVERAGE;
	if (rolling_average_this_period > 2.0 * ROLLING_AVERAGE) rolling_average_this_period = 2.0 * ROLLING_AVERAGE;

/* Calculate the new rolling average - we use a 30-day rolling average */

	pct = (double) time_in_this_period / (30.0 * 86400.0);
	ROLLING_AVERAGE = (unsigned long) ((1.0 - pct) * ROLLING_AVERAGE + pct * rolling_average_this_period + 0.5);

	/* Safeguards against excessive rolling average values */

	if (ROLLING_AVERAGE < 20) ROLLING_AVERAGE = 20;
	if (ROLLING_AVERAGE > 4000) ROLLING_AVERAGE = 4000;

/* Update rolling average data in the prime.txt file */

no_update:
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingHash, hash);
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingStartTime, (unsigned long) current_time);
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingCompleteTime, time_to_complete);
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingAverage, ROLLING_AVERAGE);
}

/* If a work_unit performed less work than estimated (say by unexpectedly */
/* finding a factor) then do not update the rolling average this period. */

void invalidateNextRollingAverageUpdate (void)
{
	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_RollingStartTime, 0);
	adjust_rolling_average ();
}

/**************************************************************/
/*      Routines to aid in reading and writing save files     */
/**************************************************************/

/* Generate temporary file name */

void tempFileName (
	struct work_unit *w,
	char	*buf)
{
	char	c;
	unsigned long p;

/* WARNING:  Version 24 had a bug where exponents between 61 million and */
/* 70 million used funky characters in the file name.  I've fixed the bug */
/* here, but users will have to rename some intermediate files. */

	p = w->n;
	if (p < 80000000) {
		sprintf (buf, "p%07li", p % 10000000);
		if (p >= 10000000)	/* buf[1] ranges from A-Y */
			buf[1] = (char) ('A' + (p / 1000000) - 10);
		if (p >= 35000000)	/* buf[2] ranges from A-Z */
			c = buf[1], buf[1] = buf[2], buf[2] = (char)(c - 25);
		if (p >= 61000000)	/* buf[3] ranges from B-T */
			c = buf[2], buf[2] = buf[3], buf[3] = (char)(c - 25);
	} else
		sprintf (buf, "p%ld", p);

/* Use different first letters for different work types.  This isn't */
/* completely compatible with v24 (the -An extension and P-1 and ECM when c=1 */

	if (w->work_type == WORK_FACTOR) buf[0] = 'f';
	if (w->work_type == WORK_ECM) buf[0] = 'e';
	if (w->work_type == WORK_PMINUS1 || w->work_type == WORK_PFACTOR) buf[0] = 'm';
	if (w->work_type == WORK_PPLUS1) buf[0] = 'n';
	if (w->work_type == WORK_CERT) buf[0] = 'c';

/* Prior to version 25.9 build 5, the pfactor work type used p as the */
/* first letter, we now use m.  To reduce upgrading problems, old save */
/* file names are renamed. */

	if (w->work_type == WORK_PFACTOR) {
		char	v258_filename[32];
		sprintf (v258_filename, "p%s", buf+1);
		rename (v258_filename, buf);
	}

/* Prior to version 25.9 build 4, if c was 1 then P-1 and ECM used */
/* a different first letter in the filename.  From now on, we will no */
/* longer do this.  To reduce upgrading problems, old save file names */
/* are renamed. */

	if (w->c == 1 && buf[0] == 'm') {
		char	v258_filename[32];
		sprintf (v258_filename, "l%s", buf+1);
		rename (v258_filename, buf);
	}
	if (w->c == 1 && buf[0] == 'e') {
		char	v258_filename[32];
		sprintf (v258_filename, "d%s", buf+1);
		rename (v258_filename, buf);
	}

/* Prior to version 25.9 build 4, we did not use k or c in generating the */
/* filename.  Thus, 10223*2^11111111+1 and 67607*2^11111111+1 would both */
/* use the same save file -- a definite problem for Seventeen or Bust. */
/* From now on, we will use k and c to generate the filename.  To reduce */
/* upgrading problems, old save file names are renamed. */

	if (w->k != 1.0 || labs(w->c) != 1) {
		char	v258_filename[32];
		strcpy (v258_filename, buf);
		buf[1] = 0;
		if (w->k != 1.0) sprintf (buf+strlen(buf), "%g", fmod (w->k, 1000000.0));
		sprintf (buf+strlen(buf), "_%ld", p);
		if (labs(w->c) != 1) sprintf (buf+strlen(buf), "_%ld", labs(w->c) % 1000);
		rename (v258_filename, buf);
		if (buf[0] == 'p') {
			v258_filename[0] = buf[0] = 'q';
			rename (v258_filename, buf);
			buf[0] = 'p';
		}
	}

/* Append extension */

	if (w->extension[0]) {
		strcat (buf, ".");
		strcat (buf, w->extension);
	}
}

/* See if the given file exists */

int fileExists (
	const char *filename)
{
	int	fd;
	fd = _open (filename, _O_RDONLY | _O_BINARY);
	if (fd < 0) return (0);
	_close (fd);
	return (1);
}

/* Create a file name from an optional directory name and a file name.  The directory name */
/* may or may not end in a slash. */

void DirPlusFilename (
	char	*dir,				// Full pathname returned here
	const char *filename)
{
	if (dir[0]) {
		char	slash = getDirectorySeparator ();
		int	len = (int) strlen (dir);

		// Append slash if not already there
		if (dir[len-1] != slash) {
			dir[len] = slash;
			dir[len+1] = 0;
		}
	}
	strcat (dir, filename);
}

/* Routines to read and write a byte array from and to a save file */

int read_array (
	int	fd,
	char	*buf,
	size_t	len,
	uint32_t *sum)
{
	size_t	i;
	unsigned char *ubuf;

	if (__read (fd, buf, len) != len) return (FALSE);
	ubuf = (unsigned char *) buf;
	if (sum != NULL) for (i = 0; i < len; i++) *sum += (uint32_t) ubuf[i];
	return (TRUE);
}

int write_array (
	int	fd,
	const char *buf,
	size_t	len,
	uint32_t *sum)
{
	size_t	i;
	unsigned char *ubuf;

	if (len == 0) return (TRUE);
	if (__write (fd, buf, len) != len) return (FALSE);
	ubuf = (unsigned char *) buf;
	if (sum != NULL) for (i = 0; i < len; i++) *sum += (uint32_t) ubuf[i];
	return (TRUE);
}

/* Routines to read and write a giant from and to a save file */

int read_giant (
	int	fd,
	giant	g,
	uint32_t *sum)
{
	unsigned long i, len, bytes;

	if (!read_long (fd, &len, sum)) return (FALSE);
	if (len == 0) return (FALSE);

	bytes = len * sizeof (uint32_t);
	if (_read (fd, g->n, bytes) != bytes) return (FALSE);
	if (len && g->n[len-1] == 0) return (FALSE);
	g->sign = len;
	*sum += (uint32_t) len;
	for (i = 0; i < len; i++) *sum += (uint32_t) g->n[i];
	return (TRUE);
}

int write_giant (
	int	fd,
	giant	g,
	uint32_t *sum)
{
	unsigned long i, len, bytes;

	len = g->sign;
	if (len == 0) {
		OutputBoth (MAIN_THREAD_NUM, "In write_giant, unexpected len == 0 failure\n");
		return (FALSE);
	}
	if (!write_long (fd, len, sum)) {
		OutputBoth (MAIN_THREAD_NUM, "In write_giant, unexpected write_long failure\n");
		return (FALSE);
	}
	bytes = len * sizeof (uint32_t);
	if (_write (fd, g->n, bytes) != bytes) {
		char	buf[200];
		sprintf (buf, "In write_giant, unexpected write failure len = %lu\n", len);
		OutputBoth (MAIN_THREAD_NUM, buf);
		return (FALSE);
	}
	*sum += (uint32_t) len;
	for (i = 0; i < len; i++) *sum += (uint32_t) g->n[i];
	return (TRUE);
}

/* Routines to read and write a gwnum from and to a save file */

int read_gwnum (
	int	fd,
	gwhandle *gwdata,
	gwnum	g,
	uint32_t *sum)
{
	giant	tmp;

	tmp = popg (&gwdata->gdata, ((int) gwdata->bit_length >> 5) + 10);
	if (tmp == NULL) return (FALSE);	// BUG - we should return some other error code
						// otherwise caller will likely delete save file.

	if (!read_giant (fd, tmp, sum)) {
		pushg (&gwdata->gdata, 1);
		return (FALSE);
	}

	gianttogw (gwdata, tmp, g);
	pushg (&gwdata->gdata, 1);
	return (TRUE);
}

int write_gwnum (
	int	fd,
	gwhandle *gwdata,
	gwnum	g,
	uint32_t *sum)
{
	giant	tmp;
	gwnum	tmp_gwnum = NULL;
	int	retcode;

	tmp = popg (&gwdata->gdata, ((int) gwdata->bit_length >> 5) + 10);
	if (tmp == NULL) {
		OutputBoth (MAIN_THREAD_NUM, "In write_gwnum, unexpected popg failure\n");
		return (FALSE);
	}
	if (!gwnum_is_not_ffted (gwdata, g)) {
		tmp_gwnum = gwalloc (gwdata);
		if (tmp_gwnum == NULL) {
			OutputBoth (MAIN_THREAD_NUM, "In write_gwnum, unexpected gwalloc failure\n");
			goto err;
		}
		gwunfft (gwdata, g, tmp_gwnum);
		g = tmp_gwnum;
	}
	retcode = gwtogiant (gwdata, g, tmp);
	if (retcode) {
		char	buf[200];
		sprintf (buf, "In write_gwnum, unexpected gwtogiant failure, retcode %d\n", retcode);
		OutputBoth (MAIN_THREAD_NUM, buf);
		goto err;
	}
	if (!write_giant (fd, tmp, sum)) goto err;
	pushg (&gwdata->gdata, 1);
	gwfree (gwdata, tmp_gwnum);
	return (TRUE);
err:	pushg (&gwdata->gdata, 1);
	gwfree (gwdata, tmp_gwnum);
	return (FALSE);
}

/* Routines to read and write values from and to a save file */

int read_int32 (
	int	fd,
	int32_t *val,
	uint32_t *sum)
{
	if (_read (fd, val, sizeof (int32_t)) != sizeof (int32_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) *val;
	return (TRUE);
}

int write_int32 (
	int	fd,
	int32_t val,
	uint32_t *sum)
{
	if (_write (fd, &val, sizeof (int32_t)) != sizeof (int32_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) val;
	return (TRUE);
}

int read_uint32 (
	int	fd,
	uint32_t *val,
	uint32_t *sum)
{
	if (_read (fd, val, sizeof (uint32_t)) != sizeof (uint32_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) *val;
	return (TRUE);
}

int write_uint32 (
	int	fd,
	uint32_t val,
	uint32_t *sum)
{
	if (_write (fd, &val, sizeof (uint32_t)) != sizeof (uint32_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) val;
	return (TRUE);
}

int read_int64 (
	int	fd,
	int64_t *val,
	uint32_t *sum)
{
	if (_read (fd, val, sizeof (int64_t)) != sizeof (int64_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) ((*val >> 32) + *val);
	return (TRUE);
}

int write_int64 (
	int	fd,
	int64_t val,
	uint32_t *sum)
{
	if (_write (fd, &val, sizeof (int64_t)) != sizeof (int64_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) ((val >> 32) + val);
	return (TRUE);
}

int read_uint64 (
	int	fd,
	uint64_t *val,
	uint32_t *sum)
{
	if (_read (fd, val, sizeof (uint64_t)) != sizeof (uint64_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) ((*val >> 32) + *val);
	return (TRUE);
}

int write_uint64 (
	int	fd,
	uint64_t val,
	uint32_t *sum)
{
	if (_write (fd, &val, sizeof (uint64_t)) != sizeof (uint64_t)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) ((val >> 32) + val);
	return (TRUE);
}

int read_double (
	int	fd,
	double	*val,
	uint32_t *sum)
{
	if (_read (fd, val, sizeof (double)) != sizeof (double)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) *val;
	return (TRUE);
}

int write_double (
	int	fd,
	double	val,
	uint32_t *sum)
{
	if (_write (fd, &val, sizeof (double)) != sizeof (double)) return (FALSE);
	if (sum != NULL) *sum += (uint32_t) val;
	return (TRUE);
}


int read_long (				// DEPRECATED
	int	fd,
	unsigned long *val,
	uint32_t *sum)
{
	uint32_t tmp;
	int retval = read_uint32 (fd, &tmp, sum);
	*val = tmp;
	return (retval);
}

int read_slong (			// DEPRECATED
	int	fd,
	long	*val,
	uint32_t *sum)
{
	int32_t tmp;
	int retval = read_int32 (fd, &tmp, sum);
	*val = tmp;
	return (retval);
}

int read_int (				// DEPRECATED
	int	fd,
	int	*val,
	uint32_t *sum)
{
	int32_t tmp;
	int retval = read_int32 (fd, &tmp, sum);
	*val = tmp;
	return (retval);
}

/* Routines to read and write the common header portion of all save files */
/* The save file format is: */
/*	u32		magic number  (different for ll, p-1, prp, tf, ecm) */
/*	u32		version number */
/*	double		k in k*b^n+c */
/*	u32		b in k*b^n+c */
/*	u32		n in k*b^n+c */
/*	s32		c in k*b^n+c */
/*	double		pct complete */
/*	char(11)	stage */
/*	char(1)		pad */
/*	u32		checksum of all following data */

/* Read and test the "magic number" in the first 4 bytes of the file. */
/* We use this to detect save files created by this program prior to the */
/* common header format. */

int read_magicnum (
	int	fd,
	uint32_t magicnum)
{
	uint32_t filenum;

/* Read the magic number from the first 4 bytes */

	_lseek (fd, 0, SEEK_SET);
	if (!read_uint32 (fd, &filenum, NULL)) return (FALSE);

/* Return TRUE if the magic number matches the caller's desired magic number */

	return (filenum == magicnum);
}

/* Read the rest of the common header */

int read_header (
	int	fd,
	uint32_t *version,
	struct work_unit *w,
	uint32_t *sum)
{
	double	k;
	unsigned long b, n;
	long	c;
	char	pad;
	char	stage[10];
	double	pct_complete;
	uint32_t trash_sum;

/* Skip past the magic number in the first 4 bytes */

	_lseek (fd, sizeof (uint32_t), SEEK_SET);

/* Read the header */

	if (!read_uint32 (fd, version, NULL)) return (FALSE);
	if (!read_double (fd, &k, NULL)) return (FALSE);
	if (!read_long (fd, &b, NULL)) return (FALSE);
	if (!read_long (fd, &n, NULL)) return (FALSE);
	if (!read_slong (fd, &c, NULL)) return (FALSE);
	if (!read_array (fd, stage, 10, NULL)) return (FALSE);
	if (!read_array (fd, &pad, 1, NULL)) return (FALSE);
	if (!read_array (fd, &pad, 1, NULL)) return (FALSE);
	if (!read_double (fd, &pct_complete, NULL)) return (FALSE);
	if (sum == NULL) sum = &trash_sum;
	if (!read_uint32 (fd, sum, NULL)) return (FALSE);

/* Validate the k,b,n,c values */

	if (k != w->k || b != w->b || n != w->n || c != w->c) return (FALSE);

/* Set the work unit's stage and pct_complete fields */

	stage[9] = 0;
	strcpy (w->stage, stage);
	if (pct_complete < 0.0) pct_complete = 0.0;
	if (pct_complete > 1.0) pct_complete = 1.0;
	w->pct_complete = pct_complete;

/* Return success */

	return (TRUE);
}

int write_header (
	int	fd,
	uint32_t magicnum,
	uint32_t version,
	struct work_unit *w)
{
	char	pad = 0;
	uint32_t sum = 0;

	if (!write_uint32 (fd, magicnum, NULL)) return (FALSE);
	if (!write_uint32 (fd, version, NULL)) return (FALSE);
	if (!write_double (fd, w->k, NULL)) return (FALSE);
	if (!write_long (fd, w->b, NULL)) return (FALSE);
	if (!write_long (fd, w->n, NULL)) return (FALSE);
	if (!write_slong (fd, w->c, NULL)) return (FALSE);
	if (!write_array (fd, w->stage, 10, NULL)) return (FALSE);
	if (!write_array (fd, &pad, 1, NULL)) return (FALSE);
	if (!write_array (fd, &pad, 1, NULL)) return (FALSE);
	if (!write_double (fd, w->pct_complete, NULL)) return (FALSE);
	if (!write_uint32 (fd, sum, NULL)) return (FALSE);
	return (TRUE);
}

#define CHECKSUM_OFFSET	48

int read_checksum (
	int	fd,
	uint32_t *sum)
{
	_lseek (fd, CHECKSUM_OFFSET, SEEK_SET);
	if (!read_uint32 (fd, sum, NULL)) return (FALSE);
	return (TRUE);
}

int write_checksum (
	int	fd,
	uint32_t sum)
{
	_lseek (fd, CHECKSUM_OFFSET, SEEK_SET);
	if (!write_uint32 (fd, sum, NULL)) return (FALSE);
	return (TRUE);
}



/* Format a message for writing to the results file and sending to the */
/* server.  We prepend the assignment ID to the message.  If operating */
/* offline, then prepend other information. */

void formatMsgForResultsFile (
	char	*buf,		/* Msg to prepend to, 200 characters max */
	struct work_unit *w)
{
	char	newbuf[2000];

/* Output a USERID/COMPID prefix for result messages */

	if (!USERID[0])
		strcpy (newbuf, buf);
	else if (!COMPID[0])
		sprintf (newbuf, "UID: %s, %s", USERID, buf);
	else
		sprintf (newbuf, "UID: %s/%s, %s", USERID, COMPID, buf);

/* Output the assignment ID too.  That alone should be enough info to */
/* credit the correct userID.  However, we still output the user ID as it */
/* is far more human friendly than an assignment ID. */

	if (w->assignment_uid[0])
		sprintf (newbuf + strlen (newbuf) - 1,
			 ", AID: %s\n", w->assignment_uid);

/* Now truncate the message to 200 characters */

	newbuf[200] = 0;
	strcpy (buf, newbuf);
}

/* Open a results file and write a line to the end of it. */

int writeResultsInternal (
	int	which_results_file,	/* 0 = results.txt, 1 = results.bench.txt, 2 = results.json.txt */
	const char *msg,
	int	output_nl)
{
static	time_t	last_time[3] = {0};
	int	fd;

/* Sanity check the input argument */

	if (which_results_file < 0 || which_results_file > 2) which_results_file = 0;

/* Open file, position to end */

	gwmutex_lock (&OUTPUT_MUTEX);
	fd = _open (RESFILES[which_results_file], _O_TEXT | _O_RDWR | _O_CREAT | _O_APPEND, CREATE_FILE_ACCESS);
	if (fd < 0) {
		gwmutex_unlock (&OUTPUT_MUTEX);
		LogMsg ("Error opening results file to output this message:\n");
		LogMsg (msg);
		return (FALSE);
	}

/* If it has been at least 5 minutes (a user-adjustable value) since the last time stamp was output, then output a new timestamp. */
/* The exception is JSON output which already contains a timestamp. */

	if (LOGTIMESTAMPING && which_results_file != 2) {
		time_t	this_time;

/* Get the interval user-settable parameter for seconds that must have elapsed since the last time the date was output */

		int write_interval = IniGetInt (INI_FILE, "ResultsFileTimestampInterval", 300);

/* If interval exceeded, output a timestamp */

		time (&this_time);
		if (write_interval && this_time - last_time[which_results_file] > (time_t) write_interval) {
			char	tmpbuf[200], buf[203];
			last_time[which_results_file] = this_time;
			timestamp (tmpbuf, 1);
			sprintf (buf, "[%s]\n", tmpbuf);
			(void) _write (fd, buf, (int) strlen (buf));
		}
	}

/* Output the message */

	if (_write (fd, msg, (unsigned int) strlen (msg)) < 0) goto fail;
	if (output_nl && _write (fd, "\n", 1) < 0) goto fail;
	_close (fd);
	gwmutex_unlock (&OUTPUT_MUTEX);
	return (TRUE);

/* On a write error, close file and return error flag */

fail:	_close (fd);
	gwmutex_unlock (&OUTPUT_MUTEX);
	LogMsg ("Error writing message to results file:\n");
	LogMsg (msg);
	return (FALSE);
}

/* Open the results file and write a line to the end of it. */

int writeResults (
	const char *msg)
{
	return (writeResultsInternal (0, msg, FALSE));
}

/* Open the results.bench file and write a line to the end of it. */

int writeResultsBench (
	const char *msg)
{
	return (writeResultsInternal (1, msg, FALSE));
}

/* Open the results.json file and write a line to the end of it. */

int writeResultsJSON (
	const char *msg)
{
	return (writeResultsInternal (2, msg, TRUE));
}


/****************************************************************************/
/*               Spool File and Server Communication Code                   */
/****************************************************************************/

#define	SPOOL_FILE_MAGICNUM	0x73d392ac
#define SPOOL_FILE_VERSION	1
/* Offset to the header words (just past the magicnum and version num) */
#define SPOOL_FILE_HEADER_OFFSET (2 * sizeof (uint32_t))
/* Offset to messages (past magicnum, version num, and two header words) */
#define SPOOL_FILE_MSG_OFFSET (4 * sizeof (uint32_t))

gwthread COMMUNICATION_THREAD = 0;	/* Handle for comm thread */
gwthread UPLOAD_THREAD = 0;		/* Handle for proof file upload thread */
gwmutex	SPOOL_FILE_MUTEX;		/* Lock governing spool file access */
int	GET_PING_INFO = 0;		/* Flag to get ping info */
int	GLOBAL_SEND_MSG_COUNT = 0;	/* Used to detect hung comm threads */
struct work_unit *LOCKED_WORK_UNIT = NULL; /* Work unit to unlock if comm */
					/* thread hangs. */

/* Spool file header word flags */

#define HEADER_FLAG_MSGS	0x0001	/* informational msgs in file */
#define HEADER_FLAG_PO		0x0004	/* exchange program options */
#define HEADER_FLAG_END_DATES	0x0008	/* completion dates need sending */
#define HEADER_FLAG_QUIT_GIMPS	0x0010	/* return all work (quit gimps) */
#define HEADER_FLAG_UC		0x0020	/* computer info has changed */
#define HEADER_FLAG_WORK_QUEUE	0x0040	/* check if enough work is queued */

void communicateWithServer (void *arg);

/* Init the spool file and communication code */

void init_spool_file_and_comm_code (void)
{
	gwmutex_init (&SPOOL_FILE_MUTEX);
	GET_PING_INFO = 0;
}

/* Add or delete the comm timers based on the communication global */
/* variables.  This is called at start up and whenever the user toggles */
/* the USE_PRIMENET or MANUAL_COMM global variables. */

void set_comm_timers (void)
{
	time_t	last_time, current_time;

/* If we are not doing automatic server communications, then make sure */
/* no communication timers are active.  If, by chance, we are in the */
/* middle of communicating with the server, then let the thread complete. */

//bug - should we destroy the comm window if !USE_PRIMENET??? User may
//have interesting data there, however it is in prime.log too.
//Maybe just change the window title.
// or do we need to leave it active until a Quit GIMPS succeeds??
	if (!USE_PRIMENET) {
		delete_timed_event (TE_COMM_SERVER);
		delete_timed_event (TE_COMPLETION_DATES);
		delete_timed_event (TE_WORK_QUEUE_CHECK);
		return;
	}
	if (MANUAL_COMM)
		delete_timed_event (TE_COMM_SERVER);

/* Create and name all the communication window. */

	create_window (COMM_THREAD_NUM);
	base_title (COMM_THREAD_NUM, "Communication thread");
	ChangeIcon (COMM_THREAD_NUM, IDLE_ICON);
	title (COMM_THREAD_NUM, "Inactive");

/* Update completion dates on the server if it has been a month since we */
/* last updated the server.  Otherwise, start a timer to make this happen */
/* at the appropriate time. */

/* Get the current time and when the completion dates were last sent */

	time (&current_time);
	last_time = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_LastEndDatesSent, 0);

/* If it's been the correct number of days, then update the end dates */

	if (current_time < last_time ||
	    current_time > (time_t)(last_time + DAYS_BETWEEN_CHECKINS * 86400.0))
		UpdateEndDates ();
	else
		add_timed_event (TE_COMPLETION_DATES, (int) (last_time + DAYS_BETWEEN_CHECKINS * 86400.0 - current_time));

/* Add the event that checks for CERT work and if the work queue has enough work.  As a side effect, this will */
/* also start the comm thread in case there is an old spool file hanging around. */

	add_timed_event (TE_WORK_QUEUE_CHECK, 5);  /* Start in 5 seconds */
}

/* Routine to fire up the communication thread in response to a user request to communicate with the server now. */

void do_manual_comm_now (void)
{
	gwmutex_lock (&SPOOL_FILE_MUTEX);
	this_is_a_manual_comm = TRUE;
	if (!COMMUNICATION_THREAD) gwthread_create (&COMMUNICATION_THREAD, &communicateWithServer, NULL);
	gwmutex_unlock (&SPOOL_FILE_MUTEX);
}

/* Clear rate-limiting counters and timers.  Any time the user explicitly */
/* chooses Test/Continue, we reset the rate limits.  We do this so that */
/* if there is an explicit need to communicate with the server frequently */
/* in the short term, the user has a way to do it.  The rate limits are */
/* here to guard against runaway clients from pummeling the server. */

void clear_comm_rate_limits (void)
{
//bug - clear other rate limiters here....

/* If we are paused for an hour because of a failed connection attempt, */
/* then kill the timer and comm now.  The user may have edited the proxy */
/* info in the INI file so that comm will now work. */

	gwmutex_lock (&SPOOL_FILE_MUTEX);
	if (!MANUAL_COMM &&
	    !COMMUNICATION_THREAD &&
	    is_timed_event_active (TE_COMM_SERVER)) {
		delete_timed_event (TE_COMM_SERVER);
		gwthread_create (&COMMUNICATION_THREAD, &communicateWithServer, NULL);
	}
	gwmutex_unlock (&SPOOL_FILE_MUTEX);
}

/* Ping the server and retrieve info for an About box. */
/* The communication thread will call pingServerResponse with the results. */

void pingServer (void)
{

/* Set global variable indicating we want to get ping information. */
/* Then fire up the communication thread. */

	gwmutex_lock (&SPOOL_FILE_MUTEX);
	GET_PING_INFO = 1;
	if (!COMMUNICATION_THREAD) gwthread_create (&COMMUNICATION_THREAD, &communicateWithServer, NULL);
	gwmutex_unlock (&SPOOL_FILE_MUTEX);
}

/* Update completion dates on the server.  Set a flag in */
/* the spool file saying this is necessary. */

void UpdateEndDates (void)
{
	spoolMessage (PRIMENET_ASSIGNMENT_PROGRESS, NULL);
}

/* Write a message to the spool file */

void spoolMessage (
	short	msgType,
	void	*msg)
{
	int	fd;
	unsigned long magicnum, version;
	unsigned long header_word;

/* If we're not using primenet, ignore this call */

	if (!USE_PRIMENET) return;

/* Obtain mutex before accessing spool file */

	gwmutex_lock (&SPOOL_FILE_MUTEX);

/* Open the spool file */

	fd = _open (SPOOL_FILE, _O_RDWR | _O_BINARY | _O_CREAT, CREATE_FILE_ACCESS);
	if (fd < 0) {
		LogMsg ("ERROR: Unable to open spool file.\n");
		gwmutex_unlock (&SPOOL_FILE_MUTEX);
		return;
	}

/* If the file is empty, write the spool file header */

	if (!read_long (fd, &magicnum, NULL)) {
		write_long (fd, SPOOL_FILE_MAGICNUM, NULL);
		write_long (fd, SPOOL_FILE_VERSION, NULL);
		write_long (fd, 0, NULL);
		write_long (fd, 0, NULL);
		header_word = 0;
	}

/* Otherwise, read and validate header.  If it is bad try to salvage */
/* the spool file data. */

	else if (magicnum != SPOOL_FILE_MAGICNUM ||
		 !read_long (fd, &version, NULL) ||
		 version != SPOOL_FILE_VERSION ||
		 !read_long (fd, &header_word, NULL)) {
		_close (fd);
		gwmutex_unlock (&SPOOL_FILE_MUTEX);
		salvageCorruptSpoolFile ();
		spoolMessage (msgType, msg);
		return;
	}

/* If this is a message telling us to check if enough work is queued up, */
/* then set the proper bit in the header word. */

	if (msgType == MSG_CHECK_WORK_QUEUE)
		header_word |= HEADER_FLAG_WORK_QUEUE;

/* If this is a maintain user info message, then set the header */
/* word appropriately.  At Scott's request also send computer info. */

	else if (msgType == PRIMENET_UPDATE_COMPUTER_INFO)
		header_word |= HEADER_FLAG_UC;

/* If this is a exchange program options message, then set the header */
/* word appropriately. */

	else if (msgType == PRIMENET_PROGRAM_OPTIONS) {
		header_word |= HEADER_FLAG_PO;
	}

/* If this is an update completion dates message, then set the header word */
/* appropriately.  At Scott's request also send computer info. */

	else if (msgType == PRIMENET_ASSIGNMENT_PROGRESS)
		header_word |= HEADER_FLAG_END_DATES + HEADER_FLAG_UC;

/* Ugly little hack when quitting GIMPS */

	else if (msgType == MSG_QUIT_GIMPS)
		header_word |= HEADER_FLAG_QUIT_GIMPS;

/* Otherwise this is a result, interim residue, unreserve, or benchmark data */

	else
		header_word |= HEADER_FLAG_MSGS;

/* Write the new header word */

	_lseek (fd, SPOOL_FILE_HEADER_OFFSET, SEEK_SET);
	write_long (fd, header_word, NULL);

/* Write out a full message */

	if (msgType == -PRIMENET_ASSIGNMENT_PROGRESS ||
	    msgType == PRIMENET_ASSIGNMENT_RESULT ||
	    msgType == PRIMENET_ASSIGNMENT_UNRESERVE ||
	    msgType == PRIMENET_BENCHMARK_DATA) {
		char	buf[1024];
		short	datalen;

/* Skip the remaining messages */

		while (_read (fd, buf, sizeof (buf)));

/* Append the latest message */

		datalen = (msgType == -PRIMENET_ASSIGNMENT_PROGRESS) ? sizeof (struct primenetAssignmentProgress) :
			  (msgType == PRIMENET_ASSIGNMENT_RESULT) ? sizeof (struct primenetAssignmentResult) :
			  (msgType == PRIMENET_ASSIGNMENT_UNRESERVE) ? sizeof (struct primenetAssignmentUnreserve) :
			  sizeof (struct primenetBenchmarkData);
		// Temporarily undo the ugly msgType hack for sending interim residues
		if (msgType == -PRIMENET_ASSIGNMENT_PROGRESS) msgType = PRIMENET_ASSIGNMENT_PROGRESS;
		(void) _write (fd, &msgType, sizeof (short));
		if (msgType == PRIMENET_ASSIGNMENT_PROGRESS) msgType = -PRIMENET_ASSIGNMENT_PROGRESS;
		(void) _write (fd, &datalen, sizeof (short));
		(void) _write (fd, msg, datalen);
	}

/* Close the spool file */

	_close (fd);

/* Fire up the communication thread if we are not waiting for the user to */
/* complete the initial startup dialog boxes and we are not doing manual */
/* communication and the communication thread is not already active and we */
/* are not waiting some time to retry after a failed communication attempt. */
/* Assignment progress reports with interim residue messages can wait. */

	if (!STARTUP_IN_PROGRESS &&
	    !MANUAL_COMM &&
	    !COMMUNICATION_THREAD &&
	    !is_timed_event_active (TE_COMM_SERVER) &&
	    msgType != -PRIMENET_ASSIGNMENT_PROGRESS)
		gwthread_create (&COMMUNICATION_THREAD, &communicateWithServer, NULL);

/* Release mutex before accessing spool file */

	gwmutex_unlock (&SPOOL_FILE_MUTEX);
}

/* Copy an existing results file to the spool file */
/* This is used when converting from manual to automatic mode */
/* It provides an extra chance that the existing results file */
/* will get sent to us. */

void spoolExistingResultsFile (void)
{
	int	i;
	char	*filename;
	char	line[256];
	FILE	*fd;

	for (i = 1; i <= 2; i++) {
		if (i == 1) filename = "results.txt";
		if (i == 2) {
			if (!strcmp (RESFILE, "results.txt")) continue;
			filename = RESFILE;
		}
		fd = fopen (filename, "r");
		if (fd == NULL) continue;
		while (fgets (line, sizeof (line) - 1, fd)) {
			if (line[0] == '[') continue;
			if (strstr (line, "Res64") == NULL &&
			    strstr (line, "factor:") == NULL &&
			    strstr (line, "completed P-1") == NULL &&
			    strstr (line, "no factor") == NULL) continue;
			if (line[0] == 'U' && strchr (line, ',') != NULL)
				safe_strcpy (line, strchr (line, ',') + 2);
//BUG - what to do with this??? Pass an unknown result type which
//forces it to be treated like a manual result?  Does v5 understand
//v4 result strings?
//			spoolMessage (PRIMENET_RESULT_MESSAGE, line);
		}
		fclose (fd);
	}
}

/* Unreserve an exponent */

int unreserve (
	unsigned long p)
{
	unsigned int tnum;
	int	rc, found_one;

/* Find exponent in worktodo.txt and delete it if present */

	found_one = FALSE;
	for (tnum = 0; tnum < NUM_WORKERS; tnum++) {
	    struct work_unit *w;

	    w = NULL;
	    for ( ; ; ) {

/* Read the line of the work file */

		w = getNextWorkToDoLine (tnum, w, SHORT_TERM_USE);
		if (w == NULL) break;
		if (w->work_type == WORK_NONE) continue;

/* Skip the line if exponent is not a match */

		if (w->n != p) continue;
		found_one = TRUE;

/* Build a packet and spool message */

		if (w->assignment_uid[0]) {
			struct primenetAssignmentUnreserve pkt;
			memset (&pkt, 0, sizeof (pkt));
			strcpy (pkt.computer_guid, COMPUTER_GUID);
			strcpy (pkt.assignment_uid, w->assignment_uid);
			spoolMessage (PRIMENET_ASSIGNMENT_UNRESERVE, &pkt);
		}

/* Delete the line.  The work unit will immediately be removed from the */
/* worktodo.txt file and will be deleted from the in-memory structures */
/* once the last in-use lock is released. */

		rc = deleteWorkToDoLine (tnum, w, TRUE);
		if (rc) return (rc);
	    }
	}

/* If we didn't find a match then output a message to the main window */

	if (!found_one) {
		char	buf[90];
		sprintf (buf, "Error unreserving exponent: %lu not found in worktodo.txt\n", p);
		OutputStr (MAIN_THREAD_NUM, buf);
	}

/* Return successfully */

	return (0);
}

/* Output message to screen and prime.log file */

void LogMsg (
	const char *str)
{
	int	fd;
	unsigned long filelen;
static	time_t	last_time = 0;
	time_t	this_time;

/* Output it to the screen */

	OutputStr (COMM_THREAD_NUM, str);

/* Open the log file and position to the end */

	gwmutex_lock (&LOG_MUTEX);
	fd = _open (LOGFILE, _O_TEXT | _O_RDWR | _O_CREAT, CREATE_FILE_ACCESS);
	if (fd < 0) {
		gwmutex_unlock (&LOG_MUTEX);
		OutputStr (COMM_THREAD_NUM, "Unable to open log file.\n");
		return;
	}
	filelen = _lseek (fd, 0L, SEEK_END);

/* If the log file has grown too big, lose the first 100,000 bytes */

	if (filelen > (unsigned long) IniGetInt (INI_FILE, "MaxLogFileSize", 2000000)) {
		char	*buf, *p;
		int	bytes_read;

		buf = (char *) malloc (filelen);
		if (buf != NULL) {
			_lseek (fd, 100000L, SEEK_SET);
			strcpy (buf, "Prior log file entries removed.\n");
			for (p = buf + strlen (buf); (bytes_read = _read (fd, p, 50000)) != 0; p += bytes_read)
				/*do nothing*/;
		       	_close (fd);
			fd = _open (LOGFILE, _O_TEXT | _O_RDWR | _O_TRUNC, CREATE_FILE_ACCESS);
			if (fd < 0) {
				free (buf);
				gwmutex_unlock (&LOG_MUTEX);
				OutputStr (COMM_THREAD_NUM, "Unable to truncate log file.\n");
				return;
			}
			(void) _write (fd, buf, (unsigned int) (p - buf));
			free (buf);
		}
	}

/* If it has been at least 5 minutes since the last time stamp was output, then output a new timestamp */

	time (&this_time);
	if (this_time - last_time > 300) {
		char	tmpbuf[50], buf[100];
		last_time = this_time;
		timestamp (tmpbuf, 1);
		sprintf (buf, "[%s - ver %s]\n", tmpbuf, VERSION);
		(void) _write (fd, buf, (unsigned int) strlen (buf));
	}

/* Output the message */

	(void) _write (fd, str, (unsigned int) strlen (str));
	_close (fd);
	gwmutex_unlock (&LOG_MUTEX);
}

/* Format text for prime.log */

void kbnc_to_text (
	char	*buf,
	int	primenet_work_type,
	int	prp_dblchk,
	double	k,
	unsigned long b,
	unsigned long n,
	long	c)
{
	char	num[80], *work_type_str;

	switch (primenet_work_type) {
	case PRIMENET_WORK_TYPE_FACTOR:
		work_type_str = "Trial factor";
		k = 1.0; b = 2; c = -1;
		break;
	case PRIMENET_WORK_TYPE_FIRST_LL:
		work_type_str = "LL";
		k = 1.0; b = 2; c = -1;
		break;
	case PRIMENET_WORK_TYPE_DBLCHK:
		work_type_str = "Double check";
		k = 1.0; b = 2; c = -1;
		break;
	case PRIMENET_WORK_TYPE_ECM:
		work_type_str = "ECM";
		break;
	case PRIMENET_WORK_TYPE_PFACTOR:
		work_type_str = "P-1";
		k = 1.0; b = 2; c = -1;
		break;
	case PRIMENET_WORK_TYPE_PMINUS1:
		work_type_str = "P-1";
		break;
	case PRIMENET_WORK_TYPE_PPLUS1:
		work_type_str = "P+1";
		break;
	case PRIMENET_WORK_TYPE_PRP:
		work_type_str = prp_dblchk ? "PRPDC" : "PRP";
		break;
	case PRIMENET_WORK_TYPE_CERT:
		work_type_str = "CERT";
		break;
	default:
		work_type_str = "Unknown work type";
		break;
	}
	gw_as_string (num, k, b, n, c);
	sprintf (buf, "%s %s", work_type_str, num);
}

/* Turn assignment ID into something prettier */

void aid_to_text (
	char	*buf,
	char	*aid)
{
	unsigned int tnum;
	struct work_unit *w;

/* Scan all work units until we find a matching assignment id */

	for (tnum = 0; tnum < NUM_WORKERS; tnum++) {
	    w = NULL;
	    for ( ; ; ) {
		w = getNextWorkToDoLine (tnum, w, SHORT_TERM_USE);
		if (w == NULL) break;
		if (w->work_type == WORK_NONE) continue;
		if (strcmp (aid, w->assignment_uid)) continue;
		gw_as_string (buf, w->k, w->b, w->n, w->c);
		decrementWorkUnitUseCount (w, SHORT_TERM_USE);
		return;
	    }
	}
	sprintf (buf, "assignment %s", aid);
}

/* Read a spooled message */

void readMessage (
	int	fd,
	long	*offset,	/* Offset of the message */
	short	*msgType,	/* 0 = no message */
	void	*msg)
{
	short	datalen;

/* Loop until a message that hasn't already been sent is found */

	for ( ; ; ) {
		*offset = _lseek (fd, 0, SEEK_CUR);
		if (_read (fd, msgType, sizeof (short)) != sizeof (short)) break;
		if (_read (fd, &datalen, sizeof (short)) != sizeof (short)) break;

/* Read the body of the message */

		if (_read (fd, msg, datalen) != datalen) break;

/* Loop if message has already been sent */

		if (*msgType == -1) continue;

/* Patch pre-v30.8 spool files where B1/B2 was stored as a double rather than a uint64_t. */
/* If top bit is on, assume the data is a double and convert it to a uint64_t. */

		if (*msgType == PRIMENET_ASSIGNMENT_RESULT) {
			struct primenetAssignmentResult *ar_msg = (struct primenetAssignmentResult *) msg;
			if (ar_msg->B1 >> 63) ar_msg->B1 = (uint64_t) *((double *) &ar_msg->B1);
			if (ar_msg->B2 >> 63) ar_msg->B2 = (uint64_t) *((double *) &ar_msg->B2);
		}

/* Return if msgType is one we expected. */

		if (*msgType == PRIMENET_ASSIGNMENT_PROGRESS ||
		    *msgType == PRIMENET_ASSIGNMENT_RESULT ||
		    *msgType == PRIMENET_ASSIGNMENT_UNRESERVE ||
		    *msgType == PRIMENET_BENCHMARK_DATA)
			return;

/* MsgType was unexpected.  This indicates a corrupt spool file. */

		LogMsg ("Corrupt spool file.  Message ignored.\n");
	}

/* On file read errors (or EOF), return code indicating we're done reading */
/* the spool file. */
	
	*msgType = 0;
}


/* Send a message that was read from the spool file */

int sendMessage (
	short	msgType,
	void	*msg)
{
	struct primenetAssignmentResult *pkt;
	int	local_send_msg_count;
	int	return_code;
	char	buf[400], info[200];

/* Load the primenet library and do any special handling */
/* required prior to calling primenet */

	if (!LoadPrimeNet ()) return (PRIMENET_ERROR_MODEM_OFF);

/* Prepend all messages with the userid and computer id */

	pkt = (struct primenetAssignmentResult *) msg;

/* Print a message on the screen and in the log file */

	switch (msgType) {
	case PRIMENET_UPDATE_COMPUTER_INFO:
		LogMsg ("Updating computer information on the server\n");
		break;
	case PRIMENET_PROGRAM_OPTIONS:
		LogMsg ("Exchanging program options with server\n");
		break;
	case PRIMENET_PING_SERVER:
		OutputStr (COMM_THREAD_NUM, "Contacting PrimeNet Server.\n");
		break;
	case PRIMENET_GET_ASSIGNMENT:
		if (!((struct primenetGetAssignment *)pkt)->get_cert_work) LogMsg ("Getting assignment from server\n");
		break;
	case PRIMENET_REGISTER_ASSIGNMENT:
		kbnc_to_text (info,
			      ((struct primenetRegisterAssignment *)pkt)->work_type, 0,
			      ((struct primenetRegisterAssignment *)pkt)->k,
			      ((struct primenetRegisterAssignment *)pkt)->b,
			      ((struct primenetRegisterAssignment *)pkt)->n,
			      ((struct primenetRegisterAssignment *)pkt)->c);
		sprintf (buf, "Registering assignment: %s\n", info);
		LogMsg (buf);
		break;
	case PRIMENET_ASSIGNMENT_PROGRESS:
		aid_to_text (info, ((struct primenetAssignmentProgress *)pkt)->assignment_uid);
		if (((struct primenetAssignmentProgress *)pkt)->iteration) {
			sprintf (buf, "Sending interim residue %d for %s\n", ((struct primenetAssignmentProgress *)pkt)->iteration, info);
			LogMsg (buf);
		} else {
			time_t	this_time;
			char	timebuf[30];
			time (&this_time);
			this_time += ((struct primenetAssignmentProgress *)pkt)->end_date;
			strcpy (timebuf, ctime (&this_time)+4);
			safe_strcpy (timebuf+6, timebuf+15);
			sprintf (buf, "Sending expected completion date for %s: %s", info, timebuf);
			LogMsg (buf);
		}
		break;
	case PRIMENET_ASSIGNMENT_UNRESERVE:
		aid_to_text (info, ((struct primenetAssignmentUnreserve *)pkt)->assignment_uid);
		sprintf (buf, "Unreserving %s\n", info);
		LogMsg (buf);
		break;
	case PRIMENET_ASSIGNMENT_RESULT:
		sprintf (buf, "Sending result to server: %s\n", ((struct primenetAssignmentResult *)pkt)->message);
		LogMsg (buf);
		break;
	case PRIMENET_BENCHMARK_DATA:
		LogMsg ("Sending benchmark data to server\n");
		break;
	}

/* Fill in the common header fields */

	pkt->versionNumber = PRIMENET_VERSION;

/* Send the message.  Kill the comm thread if server hasn't responded */
/* in 15 minutes. */

	local_send_msg_count = ++GLOBAL_SEND_MSG_COUNT;
	add_timed_event (TE_COMM_KILL, 15*60);
	return_code = PRIMENET (msgType, pkt);
	delete_timed_event (TE_COMM_KILL);

/* If the kill thread timer fired because our communication with the */
/* server hung, yet somehow the thread magically got unstuck, then return */
/* a dummy error code.  This is necessary because we have decremented the */
/* use count of the work_unit we are sending a completion date on. */

	if (GLOBAL_SEND_MSG_COUNT != local_send_msg_count) return (9999);

/* Print a result message on the screen and in the log file */

	if (return_code == 0)
	switch (msgType) {
	case PRIMENET_GET_ASSIGNMENT:
		kbnc_to_text (info,
			      ((struct primenetGetAssignment *)pkt)->work_type,
			      ((struct primenetGetAssignment *)pkt)->prp_dblchk,
			      ((struct primenetGetAssignment *)pkt)->k,
			      ((struct primenetGetAssignment *)pkt)->b,
			      ((struct primenetGetAssignment *)pkt)->n,
			      ((struct primenetGetAssignment *)pkt)->c);
		sprintf (buf, "Got assignment %s: %s\n",
			 ((struct primenetGetAssignment *)pkt)->assignment_uid,
			 info);
		LogMsg (buf);
		break;
	case PRIMENET_REGISTER_ASSIGNMENT:
		sprintf (buf, "Assignment registered as: %s\n",
			 ((struct primenetRegisterAssignment *)pkt)->assignment_uid);
		LogMsg (buf);
		break;
	}

/* Return the return code */

	return (return_code);
}


/* Get program options from the server. */

int getProgramOptions (void)
{
	struct primenetProgramOptions pkt;
	int	rc, tnum, mem_changed, restart, original_rob, mem_readable;
	unsigned int day_memory, night_memory, day_start_time, day_end_time;

/* Get the old-style memory settings */

	mem_readable = read_memory_settings (&day_memory, &night_memory,
					     &day_start_time, &day_end_time);

/* Loop once for global options (tnum = -1) and once for each worker */
/* thread (to get from the server the thread-specific options). */

	restart = FALSE;
	mem_changed = FALSE;
	original_rob = RUN_ON_BATTERY;
	for (tnum = -1; tnum < (int) NUM_WORKERS; tnum++) {

/* Init the packet.  A packet where no options are sent to the server tells */
/* the server to send us all the options saved on the server. */

		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		pkt.cpu_num = tnum;
		pkt.work_preference = -1;
		pkt.priority = -1;
		pkt.daysOfWork = -1;
		pkt.dayMemory = -1;
		pkt.nightMemory = -1;
		pkt.dayStartTime = -1;
		pkt.nightStartTime = -1;
		pkt.runOnBattery = -1;
		pkt.num_workers = -1;

/* Get the options from the server */

		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_PROGRAM_OPTIONS, &pkt);
		if (rc) return (rc);

/* The server will send all the options it has stored in its database. */
/* Copy these to our global variables and INI files. */

		if (pkt.work_preference != -1) {
			if (tnum == -1) {
				PTOSetAll (INI_FILE, "WorkPreference", KEY_SrvrPO1, WORK_PREFERENCE, pkt.work_preference);
			} else {
				PTOSetOne (INI_FILE, "WorkPreference", KEY_SrvrPO1, WORK_PREFERENCE, tnum, pkt.work_preference);
			}
		}

/* These options cannot be set for each thread.  In theory, the server */
/* should only send these options when tnum is -1 (i.e. a global option). */
/* However, for robustness, we accept the option even if the server sends */
/* as only for this thread. */

		if (pkt.priority != -1) {
			PRIORITY = pkt.priority;
			IniWriteInt (INI_FILE, "Priority", PRIORITY);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO2, PRIORITY);
			restart = TRUE;
		}

		if (pkt.daysOfWork != -1) {
			DAYS_OF_WORK = pkt.daysOfWork;
			IniWriteInt (INI_FILE, "DaysOfWork", DAYS_OF_WORK);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO3, DAYS_OF_WORK);
		}

		if (pkt.dayMemory != -1) {
			if (day_memory != pkt.dayMemory) {
				day_memory = pkt.dayMemory;
				mem_changed = TRUE;
			}
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO4, day_memory);
		}

		if (pkt.nightMemory != -1) {
			if (night_memory != pkt.nightMemory) {
				night_memory = pkt.nightMemory;
				mem_changed = TRUE;
			}
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO5, night_memory);
		}

		if (pkt.dayStartTime != -1) {
			if (day_start_time != pkt.dayStartTime) {
				day_start_time = pkt.dayStartTime;
				mem_changed = TRUE;
			}
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO6, day_start_time);
		}

		if (pkt.nightStartTime != -1) {
			if (day_end_time != pkt.nightStartTime) {
				day_end_time = pkt.nightStartTime;
				mem_changed = TRUE;
			}
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO7, day_end_time);
		}

		if (pkt.runOnBattery != -1) {
			RUN_ON_BATTERY = pkt.runOnBattery;
			IniWriteInt (INI_FILE, "RunOnBattery", RUN_ON_BATTERY);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO8, RUN_ON_BATTERY);
		}

		if (pkt.num_workers != -1) {
			if (pkt.num_workers > (int) (HW_NUM_THREADS)) pkt.num_workers = HW_NUM_THREADS;
			NUM_WORKERS = pkt.num_workers;
			IniWriteInt (INI_FILE, KEY_NumWorkers, NUM_WORKERS);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO9, NUM_WORKERS);
			restart = TRUE;
		}
		if (mem_readable && mem_changed)
			write_memory_settings (day_memory, night_memory, day_start_time, day_end_time);
	}

/* When we have finished getting the options for every CPU, then update */
/* the counter in the INI file that tracks the options counter.  The server */
/* increments the counter whenever the options are edited on the server. */
/* This updated count is sent in a uc pkt and compared to the value saved */
/* in the INI file.  If it is different we know we need to get the program */
/* options from the server. */

	IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrP00, pkt.options_counter);

/* If memory settings, priority, num_workers, or run-on-battery changed, */
/* then restart threads that may be affected by the change. */

	if (mem_readable && mem_changed) mem_settings_have_changed ();
	if (restart) stop_workers_for_restart ();
	if (original_rob != RUN_ON_BATTERY) run_on_battery_changed ();
	return (0);
}


/* Send program options to the server if they've been changed locally. */

int sendProgramOptions (
	int	*talked_to_server)
{
	struct primenetProgramOptions pkt;
	int	rc, tnum, options_changed, work_pref_changed, mem_readable;
	unsigned int local_od;
	unsigned int day_memory, night_memory, day_start_time, day_end_time;

/* Get the old-style memory settings */

	mem_readable = read_memory_settings (&day_memory, &night_memory,
					     &day_start_time, &day_end_time);

/* Loop once for global options (tnum = -1) and once for each thread */
/* to send thread-specific options. */

	for (tnum = -1; tnum < (int) NUM_WORKERS; tnum++) {

		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		pkt.cpu_num = tnum;

		work_pref_changed = FALSE;
		options_changed = FALSE;

		pkt.work_preference = -1;
		if (PTOHasOptionChanged (KEY_SrvrPO1, WORK_PREFERENCE, tnum)) {
			if (tnum == -1)
				pkt.work_preference = WORK_PREFERENCE[0];
			else
				pkt.work_preference = WORK_PREFERENCE[tnum];
			work_pref_changed = TRUE;
			options_changed = TRUE;
		}

		pkt.priority = -1;
		if (tnum == -1 && PRIORITY != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO2, -1)) {
			pkt.priority = PRIORITY;
			options_changed = TRUE;
		}

		pkt.daysOfWork = -1;
		if (tnum == -1 && DAYS_OF_WORK != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO3, -1)) {
			pkt.daysOfWork = DAYS_OF_WORK;
			options_changed = TRUE;
		}

		pkt.dayMemory = -1;
		if (tnum == -1 && mem_readable && day_memory != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO4, -1)) {
			pkt.dayMemory = day_memory;
			options_changed = TRUE;
		}

		pkt.nightMemory = -1;
		if (tnum == -1 && mem_readable && night_memory != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO5, -1)) {
			pkt.nightMemory = night_memory;
			options_changed = TRUE;
		}

		pkt.dayStartTime = -1;
		if (tnum == -1 && mem_readable && day_start_time != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO6, -1)) {
			pkt.dayStartTime = day_start_time;
			options_changed = TRUE;
		}

		pkt.nightStartTime = -1;
		if (tnum == -1 && mem_readable && day_end_time != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO7, -1)) {
			pkt.nightStartTime = day_end_time;
			options_changed = TRUE;
		}

		pkt.runOnBattery = -1;
		if (tnum == -1 && RUN_ON_BATTERY != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO8, -1)) {
			pkt.runOnBattery = RUN_ON_BATTERY;
			options_changed = TRUE;
		}

		pkt.num_workers = -1;
		if (tnum == -1 && NUM_WORKERS != IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrPO9, -1)) {
			pkt.num_workers = NUM_WORKERS;
			options_changed = TRUE;
		}

/* If options haven't changed, we're done */

		if (!options_changed) continue;

/* Send the changed options */
	
		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_PROGRAM_OPTIONS, &pkt);
		if (rc) return (rc);
		*talked_to_server = TRUE;

/* Now save the shadow copy of the changed options in our prime.txt file so we can detect future changes to the program */
/* options (even if user hand edits prime.txt!) */

		if (work_pref_changed) {
			if (tnum == -1)
				PTOSetAll (INI_FILE, "WorkPreference", KEY_SrvrPO1, WORK_PREFERENCE, WORK_PREFERENCE[0]);
			else
				PTOSetOne (INI_FILE, "WorkPreference", KEY_SrvrPO1, WORK_PREFERENCE, tnum, WORK_PREFERENCE[tnum]);
		}
		if (tnum == -1) {
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO2, PRIORITY);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO3, DAYS_OF_WORK);
			if (mem_readable) {
				IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO4, day_memory);
				IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO5, night_memory);
				IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO6, day_start_time);
				IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO7, day_end_time);
			}
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO8, RUN_ON_BATTERY);
			IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrPO9, NUM_WORKERS);
		}

/* Increment the options counter so that we can detect when the options */
/* are changed on the server.  The update we just did incremented the */
/* server's option counter.  We don't want to simply replace the options */
/* counter with the one in the pkt because of this scenario:  All synced at */
/* od=25, web page options change makes od=26, local change causes us to */
/* send new option above, the od value returned in the pkt is now 27. */
/* If we write 27 to our ini file then we will miss downloading the web */
/* change that caused the counter to become 26.  We blend the packet od */
/* value with the INI file value for maximum robustness (provides some */
/* protection from the INI file value somehow getting larger than the */
/* returned od value - causing us to miss future web page updates). */

		local_od = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrP00, -1) + 1;
		if (local_od > pkt.options_counter) local_od = pkt.options_counter;
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrP00, local_od);
	}
	return (0);
}


/* Send any queued up messages to the server.  See if we have enough */
/* work queued up.  If we have too much work, give some back */

#define RETRY_EXCEEDED "Retry count exceeded.\n"

void communicateWithServer (void *arg)
{
static	int	obsolete_client = FALSE;
static	int	send_message_retry_count = 0;
	unsigned long magicnum, version;
	unsigned long header_words[2];/* Flag words from spool file */
				/* We copy the header word to detect */
				/* any changes to the header word while */
				/* we are communicating with the server */
	int	fd;		/* Spool file handle */
	long	msg_offset;	/* File offset of current message */
	unsigned int tnum;
	double	est, work_to_get, unreserve_threshold;
	int	rc, stop_reason;
	int	talked_to_server = FALSE;
	int	can_get_cert_work, can_get_small_cert_work, sent_prp_result;
	int	server_options_counter, retry_count;
	char	buf[1000];

/* If we got an obsolete client error code earlier, then do not attempt */
/* any more communication with the server. */

	if (obsolete_client) goto leave;

/* Change title to show comm thread is active */

	ChangeIcon (COMM_THREAD_NUM, WORKING_ICON);
	title (COMM_THREAD_NUM, "Active");

/* There have been reports of computers losing their names. */
/* I can only see this happening if COMPID gets clobbered.  To combat this */
/* possibility, reread the computer name from the INI file. */

	if (COMPID[0] == 0) {
		IniGetString (INI_FILE, "ComputerID", COMPID, sizeof (COMPID), NULL);
		sanitizeString (COMPID);
	}

/* This is the retry entry point.  Used when an error causes us to go back */
/* and start reprocessing from the header words.  Also used when we get all */
/* done and find that someone wrote to the spool file while this thread was */
/* running. */

	retry_count = 0;
retry:

/* Ping the server if so requested. */

	if (GET_PING_INFO == 1) {
		struct primenetPingServer pkt;
		memset (&pkt, 0, sizeof (pkt));
//bug		pkt.versionNumber = PRIMENET_VERSION;
		pkt.ping_type = 0;
		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_PING_SERVER, &pkt);
		OutputStr (MAIN_THREAD_NUM, "\n");
		if (rc)
			OutputStr (MAIN_THREAD_NUM, "Failure pinging server");
		else
			OutputStr (MAIN_THREAD_NUM, pkt.ping_response);
		OutputStr (MAIN_THREAD_NUM, "\n");
		GET_PING_INFO = 0;
	}

/* Obtain the lock controlling spool file access.  Open the spool file. */

	gwmutex_lock (&SPOOL_FILE_MUTEX);
	fd = _open (SPOOL_FILE, _O_RDWR | _O_BINARY);
	if (fd < 0) goto locked_leave;

/* Read and validate the spool file header */

	if (!read_long (fd, &magicnum, NULL) ||
	    magicnum != SPOOL_FILE_MAGICNUM ||
	    !read_long (fd, &version, NULL) ||
	    version != SPOOL_FILE_VERSION ||
	    !read_long (fd, &header_words[0], NULL) ||
	    !read_long (fd, &header_words[1], NULL)) {
		_close (fd);
		gwmutex_unlock (&SPOOL_FILE_MUTEX);
		salvageCorruptSpoolFile ();
		goto retry;
	}

/* We must perform some complicated shenanigans to detect any changes to */
/* header word while this thread is running.  The header word is copied */
/* to the second word and the first word cleared.  That way any new calls */
/* to spoolMessage will set the first header word.  If we happen to */
/* crash then we must be careful not to lose the unprocessed bits in */
/* the second header word. */

	header_words[1] |= header_words[0];
	header_words[0] = 0;
	_lseek (fd, SPOOL_FILE_HEADER_OFFSET, SEEK_SET);
	write_long (fd, header_words[0], NULL);
	write_long (fd, header_words[1], NULL);

/* Close and unlock the spool file.  This allows workers to write messages to the spool file while we are sending messages. */

	_close (fd);
	gwmutex_unlock (&SPOOL_FILE_MUTEX);

/* Make sure we don't pummel the server with data.  Suppose user uses */
/* Advanced/Factor to find tiny factors again.  At least, make sure */
/* he only sends the data once every 5 minutes. */

//bug	next_comm_time = this_time + 300;  //bug - manual_comm overrides this
// move this test to SendMessage?  It could spawn add_timed_event
// rather than creating this thread.  However, does spreading out these
// messages help?  No.  It only helps if prime95 is buggy (like the spool file
// can't be deleted or modified so it just keeps being resent?)

//bug - check this global before each message is sent... in case ping is requested while
// a comm is in progress

/* Send computer info first */

	server_options_counter = -1;
	if (header_words[1] & HEADER_FLAG_UC) {
		struct primenetUpdateComputerInfo pkt;
		unsigned long server_uid, server_computer_name;

		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		if (!IniGetInt (INI_FILE, "FixedHardwareUID", 0)) {
			strcpy (pkt.hardware_guid, HARDWARE_GUID);
			strcpy (pkt.windows_guid, WINDOWS_GUID);
		}
		generate_application_string (pkt.application);
		strcpy (pkt.cpu_model, CPU_BRAND);
		pkt.cpu_features[0] = 0;
//		if (CPU_FLAGS & CPU_RDTSC) strcat (pkt.cpu_features, "RDTSC,");
//		if (CPU_FLAGS & CPU_CMOV) strcat (pkt.cpu_features, "CMOV,");
		if (CPU_FLAGS & CPU_PREFETCH) strcat (pkt.cpu_features, "Prefetch,");
		if (CPU_FLAGS & CPU_3DNOW) strcat (pkt.cpu_features, "3DNow!,");
//		if (CPU_FLAGS & CPU_MMX) strcat (pkt.cpu_features, "MMX,");
		if (CPU_FLAGS & CPU_SSE) strcat (pkt.cpu_features, "SSE,");
		if (CPU_FLAGS & CPU_SSE2) strcat (pkt.cpu_features, "SSE2,");
		if (CPU_FLAGS & CPU_SSE41) strcat (pkt.cpu_features, "SSE4,");
		if (CPU_FLAGS & CPU_AVX) strcat (pkt.cpu_features, "AVX,");
		if (CPU_FLAGS & CPU_AVX2) strcat (pkt.cpu_features, "AVX2,");
		if (CPU_FLAGS & (CPU_FMA3 | CPU_FMA4)) strcat (pkt.cpu_features, "FMA,");
		if (CPU_FLAGS & CPU_AVX512F) strcat (pkt.cpu_features, "AVX512F,");
		if (pkt.cpu_features[0])
			pkt.cpu_features[strlen (pkt.cpu_features) - 1] = 0;
		pkt.L1_cache_size = CPU_L1_CACHE_SIZE;
		pkt.L2_cache_size = CPU_L2_CACHE_SIZE;
		pkt.L3_cache_size = CPU_L3_CACHE_SIZE;
		pkt.num_cpus = HW_NUM_CORES;
		pkt.num_hyperthread = divide_rounding_up (HW_NUM_THREADS, HW_NUM_CORES);
		pkt.mem_installed = physical_memory ();
		pkt.cpu_speed = (int) CPU_SPEED;
		pkt.hours_per_day = CPU_HOURS;
		pkt.rolling_average = ROLLING_AVERAGE;
		/* The spec says only send the UserID if it has changed. */
		/* Rather than store the UID that we last sent to the server */
		/* we store a hash value so the user is not tempted to hand edit it. */
		server_uid = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrUID, 0);
		if (string_to_hash (USERID) != server_uid) strcpy (pkt.user_id, USERID);
		/* The spec says only send the computer name if it changed */
		/* Again store a hash value so the user won't hand edit the value */
		server_computer_name = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrComputerName, 0);
		if (string_to_hash (COMPID) != server_computer_name) strcpy (pkt.computer_name, COMPID);
		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_UPDATE_COMPUTER_INFO, &pkt);
		if (rc) goto error_exit;
		talked_to_server = TRUE;
		strcpy (USERID, pkt.user_id);
		IniWriteString (INI_FILE, "V5UserID", USERID);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrUID, string_to_hash (USERID));
		strcpy (COMPID, pkt.computer_name);
		IniWriteString (INI_FILE, "ComputerID", COMPID);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_SrvrComputerName, string_to_hash (COMPID));
		server_options_counter = pkt.options_counter;
	}

/* When we first register a computer, the server may have initialized */
/* some default program options for us to download.  Get them before */
/* we overwrite them with our default option values. */

//bug - who wins when user has also set some options locally during the
//startup_in_progress code path?  What about options that came from a
// copied prime.txt file - which take precedence?
	if (server_options_counter == 1 && server_options_counter > IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrP00, 0)) {
		rc = getProgramOptions ();
		if (rc) goto error_exit;
		talked_to_server = TRUE;
	}

/* Send our changed program options.  Do this before they could get */
/* overwritten by downloading changed options on the server (since the */
/* server sends all options, not just the changed options). */

	rc = sendProgramOptions (&talked_to_server);
	if (rc) goto error_exit;

/* Finally get program options if they've been changed on the server. */

	if (server_options_counter > IniSectionGetInt (INI_FILE, SEC_Internals, KEY_SrvrP00, 0)) {
		rc = getProgramOptions ();
		if (rc) goto error_exit;
		talked_to_server = TRUE;
	}

/* Send the messages */

	msg_offset = SPOOL_FILE_MSG_OFFSET;
	sent_prp_result = FALSE;
	for ( ; ; ) {
		short	msgType;
		union {
			struct primenetAssignmentProgress ap;
			struct primenetAssignmentResult ar;
			struct primenetAssignmentUnreserve au;
			struct primenetBenchmarkData bd;
		} msg;
		long	new_offset;

/* Read a message */

		gwmutex_lock (&SPOOL_FILE_MUTEX);
		fd = _open (SPOOL_FILE, _O_RDONLY | _O_BINARY);
		if (fd < 0) goto locked_leave;
		_lseek (fd, msg_offset, SEEK_SET);
		memset (&msg, 0, sizeof (msg));		// Clear msg in case spool file was written by an older prime95 version
		readMessage (fd, &msg_offset, &msgType, &msg);
		new_offset = _lseek (fd, 0, SEEK_CUR);
		_close (fd);
		gwmutex_unlock (&SPOOL_FILE_MUTEX);

/* If there was a message, send it now.  Ignore most errors.  We do this */
/* so that a corrupt spool file will not "get stuck" trying to send the */
/* same corrupt message over and over again. */

		if (msgType == 0) break;
		for ( ; ; ) {
			LOCKED_WORK_UNIT = NULL;
			rc = sendMessage (msgType, &msg);
			/* If the computer ID is bad and not ours (a damaged */
			/* spool file or we were forced to generate a new */
			/* computer GUID?) then try again using our computer ID */
			if ((rc == PRIMENET_ERROR_UNREGISTERED_CPU ||
			     rc == PRIMENET_ERROR_CPU_CONFIGURATION_MISMATCH ||
			     rc == PRIMENET_ERROR_STALE_CPU_INFO) &&
			    strcmp (msg.ar.computer_guid, COMPUTER_GUID)) {
				OutputStr (COMM_THREAD_NUM, "Retrying message with this computer's GUID.\n");
				strcpy (msg.ar.computer_guid, COMPUTER_GUID);
				continue;
			}
			break;
		}
		if (rc >= PRIMENET_FIRST_INTERNAL_ERROR ||
		    rc == PRIMENET_ERROR_SERVER_BUSY ||
		    rc == PRIMENET_ERROR_OBSOLETE_CLIENT ||
		    rc == PRIMENET_ERROR_UNREGISTERED_CPU ||
		    rc == PRIMENET_ERROR_CPU_CONFIGURATION_MISMATCH ||
		    rc == PRIMENET_ERROR_STALE_CPU_INFO)
			goto error_exit;
		talked_to_server = TRUE;

/* Handle errors that show the server processed the message properly. */
/* These errors can happen with unwanted results (retesting known primes, */
/* PRP results, ECM or P-1 on non-Mersennes, etc.) */ 

		if (rc == PRIMENET_ERROR_INVALID_ASSIGNMENT_KEY ||
		    rc == PRIMENET_ERROR_INVALID_RESULT_TYPE ||
		    rc == PRIMENET_ERROR_NO_ASSIGNMENT ||
		    rc == PRIMENET_ERROR_WORK_NO_LONGER_NEEDED ||
		    rc == PRIMENET_ERROR_INVALID_PARAMETER)
			rc = 0;

/* Just in case the error was casued by some kind of unexpected */
/* and unrepeatable server problem, retry sending the message 5 times */
/* before giving up and moving on to the next one. */

		if (rc) {
			if (send_message_retry_count++ < 5) {
				rc = PRIMENET_ERROR_SERVER_BUSY;
				goto error_exit;
			}
			LogMsg ("Deleting unprocessed message from spool file.\n");
		}
		send_message_retry_count = 0;

/* Note the sending of a PRP result */

		if (msgType == PRIMENET_ASSIGNMENT_RESULT && (msg.ar.result_type == PRIMENET_AR_PRP_RESULT || msg.ar.result_type == PRIMENET_AR_PRP_PRIME))
			sent_prp_result = TRUE;

/* Flag the message as successfully sent.  Even if there was an error, the */
/* error code is such that resending the message will not be helpful. */

		gwmutex_lock (&SPOOL_FILE_MUTEX);
		fd = _open (SPOOL_FILE, _O_RDWR | _O_BINARY);
		if (fd < 0) goto locked_leave;
		_lseek (fd, msg_offset, SEEK_SET);
		msgType = -1;
		(void) _write (fd, &msgType, sizeof (short));
		_close (fd);
		gwmutex_unlock (&SPOOL_FILE_MUTEX);
		msg_offset = new_offset;
	}

/* If we just sent a PRP result or this is a manual comm, trigger thread that uploads proofs.  For good measure, also do this if quitting GIMPS. */

	if (sent_prp_result || this_is_a_manual_comm || (header_words[1] & HEADER_FLAG_QUIT_GIMPS) || IniGetInt (INI_FILE, KEY_QuitGIMPS, 0))
		gwevent_signal (&PROOF_UPLOAD_EVENT);
	this_is_a_manual_comm = FALSE;		// Clear ugly flag that signals manual comm

/* See if we can request certification work.  Certification work must be done ASAP to reduce disk space used by PrimeNet server. */
/* Thus, it is priority work.  Some options turn off priority work which precludes getting certification work.  Part-time computers */
/* are spared certifications.  We are only allowed one certification assignment at a time (part of our spread the load amongst many */
/* users philosophy).  Also, by default do not get PRP cofactor certs (it annoys some people to interrupt their first time or */
/* double-checks to process quick work that they believe is not part of GIMPS main purpose).  If the user is doing cofactor work, */
/* then by all means default to getting cert work on PRP cofactor proofs. */

	can_get_cert_work = (header_words[1] & HEADER_FLAG_WORK_QUEUE) && IniGetInt (INI_FILE, "CertWork", 1) && DAYS_OF_WORK > 0;
	if (WELL_BEHAVED_WORK || SEQUENTIAL_WORK == 1) can_get_cert_work = FALSE;
	if (!WORKERS_ACTIVE || WORKERS_STOPPING) can_get_cert_work = FALSE;
	if (CPU_HOURS <= 12) can_get_cert_work = FALSE;
	if (is_timed_event_active (TE_PAUSE_WHILE) || is_timed_event_active (TE_READ_PAUSE_DATA)) can_get_cert_work = FALSE;
	if (can_get_cert_work) {
		int	max_cert_assignments;
		can_get_small_cert_work = FALSE;
		max_cert_assignments = (IniGetInt (INI_FILE, "CertDailyCPULimit", 10) >= 50 ? 3 : 1);
		for (tnum = 0; tnum < NUM_WORKERS; tnum++) {
			struct work_unit *w;
			for (w = NULL; ; ) {
				w = getNextWorkToDoLine (tnum, w, SHORT_TERM_USE);
				if (w == NULL) break;
				if (w->work_type == WORK_CERT && --max_cert_assignments == 0) can_get_cert_work = FALSE;
				if (w->n < 50000000) can_get_small_cert_work = TRUE;
			}
		}
	}

/* Certification work is rate limited two ways.  By MB downloaded and by CPU time consumed */
/* Update these rate limits and disable getting certification work if either limit has been reached. */

	if (can_get_cert_work) {
		float	max_daily_MB_limit, max_daily_CPU_limit;
		float	daily_MB_limit_remaining, daily_CPU_limit_remaining;
		time_t	current_time, last_update_time;
		float	days_since_last_update;

		max_daily_MB_limit = (float) IniSectionGetInt (INI_FILE, SEC_PrimeNet, KEY_DownloadDailyLimit, 40); // Daily download limit in MB
		max_daily_CPU_limit = (float) IniGetInt (INI_FILE, "CertDailyCPULimit", 10); // Cert Daily cpu limit (1.0 = 1% of 2015 quadcore)

		daily_MB_limit_remaining = IniSectionGetFloat (INI_FILE, SEC_Internals, KEY_CertDailyMBRemaining, max_daily_MB_limit);
		daily_CPU_limit_remaining = IniSectionGetFloat (INI_FILE, SEC_Internals, KEY_CertDailyCPURemaining, max_daily_CPU_limit);

		// Calc how many days have elapsed since unused quotas (daily_MB_limit_remaining, daily_CPU_limit_remaining) were updated
		time (&current_time);
		last_update_time = IniSectionGetInt (INI_FILE, SEC_Internals, KEY_CertDailyRemainingLastUpdate, 0);
		days_since_last_update = (float) (current_time - last_update_time) / (float) 86400.0;
		if (days_since_last_update < 0.0) days_since_last_update = 0.0;
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_CertDailyRemainingLastUpdate, (unsigned long) current_time);

		// Increase remaining unused quotas based upon time elapsed since last update
		daily_MB_limit_remaining += days_since_last_update * max_daily_MB_limit;
		if (daily_MB_limit_remaining > max_daily_MB_limit) daily_MB_limit_remaining = max_daily_MB_limit;
		daily_CPU_limit_remaining += days_since_last_update * max_daily_CPU_limit;
		if (daily_CPU_limit_remaining > max_daily_CPU_limit) daily_CPU_limit_remaining = max_daily_CPU_limit;

		// Save the updated quotas
		IniSectionWriteFloat (INI_FILE, SEC_Internals, KEY_CertDailyMBRemaining, daily_MB_limit_remaining);
		IniSectionWriteFloat (INI_FILE, SEC_Internals, KEY_CertDailyCPURemaining, daily_CPU_limit_remaining);

		// Disable getting cert work if either quota exceeded
		if (daily_MB_limit_remaining <= 0.0 || daily_CPU_limit_remaining <= 0.0) can_get_cert_work = FALSE;
	}

/* Loop over all workers to get enough work for each worker */

	unreserve_threshold = IniGetInt (INI_FILE, "UnreserveDays", 30) * 86400.0;
	work_to_get = DAYS_OF_WORK * 86400.0;
	for (tnum = 0; tnum < NUM_WORKERS; tnum++) {
	    struct work_unit *w;
	    int	num_work_units;
	    int	first_work_unit_interruptable = TRUE;

/* Get work to do until we've accumulated enough to keep us busy for a while.  If we have too much work to do, lets give some back. */

	    num_work_units = 0;
	    est = 0.0;
	    for (int pass = 0; pass <= 1; pass++)
	    for (w = NULL; ; ) {
		int	registered_assignment;

/* Read the line of the work file, handle all CERTs first */

		w = getNextWorkToDoLine (tnum, w, SHORT_TERM_USE);
		if (w == NULL) break;
		if (w->work_type == WORK_NONE) continue;
		if (pass == 0 && w->work_type != WORK_CERT) continue;
		if (pass == 1 && w->work_type == WORK_CERT) continue;

/* If we are quitting GIMPS or we have too much work queued up, then return the assignment. */

		if (header_words[1] & HEADER_FLAG_QUIT_GIMPS ||
		    (est >= work_to_get + unreserve_threshold &&
		     ! isWorkUnitActive (w) &&
		     w->pct_complete == 0.0 &&
		     num_work_units >= IniGetInt (INI_FILE, "UnreserveExponents", 4))) {

			if (w->assignment_uid[0]) {
				struct primenetAssignmentUnreserve pkt;
				memset (&pkt, 0, sizeof (pkt));
				strcpy (pkt.computer_guid, COMPUTER_GUID);
				strcpy (pkt.assignment_uid, w->assignment_uid);
				LOCKED_WORK_UNIT = w;
				rc = sendMessage (PRIMENET_ASSIGNMENT_UNRESERVE, &pkt);
				if (rc && rc != PRIMENET_ERROR_INVALID_ASSIGNMENT_KEY) {
					decrementWorkUnitUseCount (w, SHORT_TERM_USE);
					goto error_exit;
				}
				talked_to_server = TRUE;
			}
			stop_reason = deleteWorkToDoLine (tnum, w, TRUE);
			if (stop_reason) goto error_exit;
			continue;
		}

/* If this is the first work unit for this worker, clear flag if it might be costly to interrupt it with priority work */
/* This would be P-1 (especially) or ECM and P+1 where stage 2 setup can be expensive.  Also PRP tests that are near done should */
/* be completed ASAP to free up the temp disk space they are using.  Actually, any work that is near done, the user might */
/* be watching with anticipation for its completion. */

		if (est == 0.0 && IniGetInt (INI_FILE, "CertDailyCPULimit", 10) < 50 &&
		    (w->work_type == WORK_PMINUS1 || w->work_type == WORK_PFACTOR || w->work_type == WORK_ECM ||
		     w->work_type == WORK_PPLUS1 || w->pct_complete > 0.85))
			first_work_unit_interruptable = FALSE;

/* Adjust our time estimate */

		num_work_units++;
		est += work_estimate (tnum, w);

/* Register assignments that were not issued by the server */

		registered_assignment = FALSE;
		if (!w->assignment_uid[0] && !w->ra_failed) {
			struct primenetRegisterAssignment pkt;
			memset (&pkt, 0, sizeof (pkt));
			strcpy (pkt.computer_guid, COMPUTER_GUID);
			pkt.cpu_num = tnum;
			if (w->work_type == WORK_FACTOR)
				pkt.work_type = PRIMENET_WORK_TYPE_FACTOR;
			if (w->work_type == WORK_PMINUS1)
				pkt.work_type = PRIMENET_WORK_TYPE_PMINUS1;
			if (w->work_type == WORK_PPLUS1)
				pkt.work_type = PRIMENET_WORK_TYPE_PPLUS1;
			if (w->work_type == WORK_PFACTOR)
				pkt.work_type = PRIMENET_WORK_TYPE_PFACTOR;
			if (w->work_type == WORK_PPLUS1)
				pkt.work_type = PRIMENET_WORK_TYPE_PPLUS1;
			if (w->work_type == WORK_ECM)
				pkt.work_type = PRIMENET_WORK_TYPE_ECM;
			if (w->work_type == WORK_TEST || w->work_type == WORK_ADVANCEDTEST)
				pkt.work_type = PRIMENET_WORK_TYPE_FIRST_LL;
			if (w->work_type == WORK_DBLCHK)
				pkt.work_type = PRIMENET_WORK_TYPE_DBLCHK;
			if (w->work_type == WORK_PRP)
				pkt.work_type = PRIMENET_WORK_TYPE_PRP;
			pkt.k = w->k;
			pkt.b = w->b;
			pkt.n = w->n;
			pkt.c = w->c;
			pkt.how_far_factored = w->sieve_depth;
			pkt.factor_to = w->factor_to;
			pkt.has_been_pminus1ed = w->pminus1ed;
			pkt.B1 = w->B1;
			pkt.B2 = w->B2;
			pkt.curves = w->curves_to_do;
			pkt.tests_saved = w->tests_saved;
			LOCKED_WORK_UNIT = w;
			rc = sendMessage (PRIMENET_REGISTER_ASSIGNMENT, &pkt);
			if (rc &&
			    rc != PRIMENET_ERROR_NO_ASSIGNMENT &&
			    rc != PRIMENET_ERROR_INVALID_ASSIGNMENT_TYPE &&
			    rc != PRIMENET_ERROR_INVALID_PARAMETER) {
				decrementWorkUnitUseCount (w, SHORT_TERM_USE);
				goto error_exit;
			}
			talked_to_server = TRUE;
			if (rc)
				w->ra_failed = TRUE;
			else {
				strcpy (w->assignment_uid, pkt.assignment_uid);
				registered_assignment = TRUE;
			}
			updateWorkToDoLine (tnum, w);
		}

/* Update the server on the work unit's projected completion date */
/* If we get an invalid assignment key, then the user probably unreserved */
/* the exponent using the web forms - delete it from our work to do file. */

		if ((header_words[1] & HEADER_FLAG_END_DATES || registered_assignment) && w->assignment_uid[0]) {
			struct primenetAssignmentProgress pkt2;
			memset (&pkt2, 0, sizeof (pkt2));
			strcpy (pkt2.computer_guid, COMPUTER_GUID);
			pkt2.cpu_num = tnum;
			strcpy (pkt2.assignment_uid, w->assignment_uid);
			strcpy (pkt2.stage, w->stage);
			pkt2.pct_complete = w->pct_complete * 100.0;
			pkt2.end_date = (unsigned long) est;
			pkt2.next_update = (uint32_t) (DAYS_BETWEEN_CHECKINS * 86400.0);
			pkt2.fftlen = w->fftlen;
			LOCKED_WORK_UNIT = w;
			rc = sendMessage (PRIMENET_ASSIGNMENT_PROGRESS, &pkt2);
			if (rc == PRIMENET_ERROR_INVALID_ASSIGNMENT_KEY || rc == PRIMENET_ERROR_WORK_NO_LONGER_NEEDED) {
				est -= work_estimate (tnum, w);
				rc = deleteWorkToDoLine (tnum, w, TRUE);
			} else if (rc) {
				decrementWorkUnitUseCount (w, SHORT_TERM_USE);
				goto error_exit;
			}
			talked_to_server = TRUE;
		}
	    }

/* If we are quitting gimps, do not get more work.  Note that there is a */
/* race condition when quitting gimps that makes it possible to get here */
/* with a request to get exponents and USE_PRIMENET not set. */

	    if (header_words[1] & HEADER_FLAG_QUIT_GIMPS || IniGetInt (INI_FILE, KEY_QuitGIMPS, 0) || !USE_PRIMENET) continue;

/* Occasionally get certification work from the server.  P-1 and ECM stage 2 may have significant costs with an interruption to do priority work. */
/* Also, lots of work_units is not a typical setup -- may indicate a large worktodo.txt file with high costs reading and writing it.  Also, check */
/* that this thread isn't stopped and provide a method of limiting CERT work to one specific worker. */

	    if (can_get_cert_work && first_work_unit_interruptable && num_work_units < 20 && ACTIVE_WORKERS[tnum] &&
		IniGetInt (INI_FILE, "CertWorker", tnum+1) == tnum+1) {
		int	num_certs = 0;

		can_get_cert_work = FALSE;	// Only one worker can get certification work

/* Get up to 5 certifications (but usually just one).  The limit of 5 is rather arbitrary. */

		while (num_certs < 5) {
			struct primenetGetAssignment pkt1;
			struct work_unit w;

/* Get a certification work unit to process */

			memset (&pkt1, 0, sizeof (pkt1));
			strcpy (pkt1.computer_guid, COMPUTER_GUID);
			pkt1.cpu_num = tnum;
			pkt1.get_cert_work = IniGetInt (INI_FILE, "CertDailyCPULimit", 10); // Helps server decide cert exponent size
			if (pkt1.get_cert_work <= 0) pkt1.get_cert_work = 1;
			pkt1.min_exp = IniGetInt (INI_FILE, "CertMinExponent", can_get_small_cert_work ? 0 : 50000000);
			pkt1.max_exp = IniGetInt (INI_FILE, "CertMaxExponent", HW_NUM_CORES / NUM_WORKERS < 4 ? 200000000 : 0);
			LOCKED_WORK_UNIT = NULL;
			rc = sendMessage (PRIMENET_GET_ASSIGNMENT, &pkt1);
			// Ignore errors, we expect this work to only be available sometimes
			if (rc) break;
			talked_to_server = TRUE;

/* Format the work_unit structure based on the work_type */

			if (pkt1.work_type != PRIMENET_WORK_TYPE_CERT) {
				sprintf (buf, "Received unknown work type (expected 200): %lu.\n", (unsigned long) pkt1.work_type);
				LogMsg (buf);
				goto error_exit;
			}
			memset (&w, 0, sizeof (w));
			w.work_type = WORK_CERT;
			strcpy (w.assignment_uid, pkt1.assignment_uid);
			w.k = 1.0;
			w.b = 2;
			w.n = pkt1.n;
			w.c = -1;
			w.cert_squarings = pkt1.num_squarings;
			num_certs++;

/* Write the exponent to our worktodo file */

			stop_reason = addWorkToDoLine (tnum, &w, ADD_TO_FRONT);
			if (stop_reason) goto error_exit;

/* Update the daily cert quotas */

			float	daily_MB_limit_remaining, daily_CPU_limit_remaining, daily_CPU_quota_used;
			daily_MB_limit_remaining = IniSectionGetFloat (INI_FILE, SEC_Internals, KEY_CertDailyMBRemaining, 40.0);
			daily_CPU_limit_remaining = IniSectionGetFloat (INI_FILE, SEC_Internals, KEY_CertDailyCPURemaining, 10.0);

			// Decrease remaining unused quotas based expected work required for this cert
			// A daily CPU limit of 1.0 is equals 1% of the work a 2015 quad-core CPU can do in a day.
			// My dream machine CPUs from that era can do 110,000 squarings of 97.3M in 1/100th of a day.
			daily_MB_limit_remaining -= (float) w.n / (float) 8388608.0;	// Expected MB to download
			daily_CPU_quota_used = (float) w.cert_squarings / (float) 110000.0;	// Adjust for num_squarings 
			daily_CPU_quota_used *= (float) pow (2.1, log2 ((float) w.n / (float) 97300000.0));	// Adjust (roughly) for FFT timing difference
			daily_CPU_limit_remaining -= daily_CPU_quota_used;

			// Save the updated quotas
			IniSectionWriteFloat (INI_FILE, SEC_Internals, KEY_CertDailyMBRemaining, daily_MB_limit_remaining);
			IniSectionWriteFloat (INI_FILE, SEC_Internals, KEY_CertDailyCPURemaining, daily_CPU_limit_remaining);

			// If quotas exceeded, get no more cert assignments
			if (daily_MB_limit_remaining <= 0.0 || daily_CPU_limit_remaining <= 0.0) break;

			// If the certification exponent is small the certification will be fast.  To avoid lots of priority work interrupts
			// to do fast work, get several small certifications at one time (or several small and one big).
			if (pkt1.n < 50000000) continue;

			// Usually get one big cert at a time.  For users that want to lump their CERTs together (perhaps to reduce the number
			// priority work interruptions), allow getting several certifications at a time.
			if (num_certs < IniGetInt (INI_FILE, "CertQuantity", 1)) continue;

			// Usually get one big cert at a time.  For the rare user that wants to do lots of certifications (they set cert CPU
			// percentage above 50%) let them get several.
			if (IniGetInt (INI_FILE, "CertDailyCPULimit", 10) < 50) break;
		}
	    }

/* If we don't have enough work to do, get more work from the server. */

	    while (est < work_to_get && num_work_units < IniGetInt (INI_FILE, "MaxExponents", 15)) {
		struct primenetGetAssignment pkt1;
		struct primenetAssignmentProgress pkt2;
		struct work_unit w;

/* Get a work unit to process */

		memset (&pkt1, 0, sizeof (pkt1));
		strcpy (pkt1.computer_guid, COMPUTER_GUID);
		pkt1.cpu_num = tnum;
		pkt1.temp_disk_space = CPU_WORKER_DISK_SPACE;
		pkt1.min_exp = IniGetInt (INI_FILE, "GetMinExponent", 0);
		pkt1.max_exp = IniGetInt (INI_FILE, "GetMaxExponent", 0);
		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_GET_ASSIGNMENT, &pkt1);
		if (rc) goto error_exit;
		talked_to_server = TRUE;

/* Sanity check that the server hasn't sent us bogus (too small) exponents to test */

		if (pkt1.n < 15000000 &&
		    (pkt1.work_type == PRIMENET_WORK_TYPE_FACTOR ||
		     pkt1.work_type == PRIMENET_WORK_TYPE_PFACTOR ||
		     pkt1.work_type == PRIMENET_WORK_TYPE_FIRST_LL ||
		     pkt1.work_type == PRIMENET_WORK_TYPE_DBLCHK)) {
			sprintf (buf, "Server sent bad exponent: %lu.\n", (unsigned long) pkt1.n);
			LogMsg (buf);
			goto error_exit;
		}

/* Format the work_unit structure based on the work_type */

		memset (&w, 0, sizeof (w));
		strcpy (w.assignment_uid, pkt1.assignment_uid);
		w.k = 1.0;	/* Set some default values */
		w.b = 2;
		w.n = pkt1.n;
		w.c = -1;
		switch (pkt1.work_type) {
		case PRIMENET_WORK_TYPE_FACTOR:
			w.work_type = WORK_FACTOR;
			w.sieve_depth = pkt1.how_far_factored;
			w.factor_to = pkt1.factor_to;
			break;
		case PRIMENET_WORK_TYPE_PFACTOR:
			w.work_type = WORK_PFACTOR;
			w.sieve_depth = pkt1.how_far_factored;
			w.pminus1ed = pkt1.has_been_pminus1ed;
			w.tests_saved = pkt1.tests_saved;
			break;
		case PRIMENET_WORK_TYPE_FIRST_LL:
			w.work_type = WORK_TEST;
			w.sieve_depth = pkt1.how_far_factored;
			w.pminus1ed = pkt1.has_been_pminus1ed;
			break;
		case PRIMENET_WORK_TYPE_DBLCHK:
			w.work_type = WORK_DBLCHK;
			w.sieve_depth = pkt1.how_far_factored;
			w.pminus1ed = pkt1.has_been_pminus1ed;
			break;
		case PRIMENET_WORK_TYPE_PMINUS1:
			w.work_type = WORK_PMINUS1;
			w.k = pkt1.k;
			w.b = pkt1.b;
			w.c = pkt1.c;
			w.B1 = pkt1.B1;
			w.B2 = pkt1.B2;
			break;
		case PRIMENET_WORK_TYPE_PPLUS1:		/* Not presently supported by the server */
			w.work_type = WORK_PPLUS1;
			w.k = pkt1.k;
			w.b = pkt1.b;
			w.c = pkt1.c;
			w.nth_run = pkt1.nth_run;
			w.B1 = pkt1.B1;
			w.B2 = pkt1.B2;
			break;
		case PRIMENET_WORK_TYPE_ECM:
			w.work_type = WORK_ECM;
			w.k = pkt1.k;
			w.b = pkt1.b;
			w.c = pkt1.c;
			w.B1 = pkt1.B1;
			w.B2 = pkt1.B2;
			w.curves_to_do = pkt1.curves;
			break;
		case PRIMENET_WORK_TYPE_PRP:
			w.work_type = WORK_PRP;
			w.k = pkt1.k;
			w.b = pkt1.b;
			w.c = pkt1.c;
			w.sieve_depth = pkt1.how_far_factored;
			w.tests_saved = pkt1.tests_saved;
			if (w.tests_saved == 2) w.tests_saved = 1.3;
			w.prp_base = pkt1.prp_base;
			w.prp_residue_type = pkt1.prp_residue_type;
			w.prp_dblchk = pkt1.prp_dblchk;
			break;
		default:
			sprintf (buf, "Received unknown work type: %lu.\n", (unsigned long) pkt1.work_type);
			LogMsg (buf);
			goto error_exit;
		}
		if (pkt1.known_factors[0]) { /* ECM, P-1, PRP may have this */
			w.known_factors = (char *) malloc (strlen (pkt1.known_factors) + 1);
			if (w.known_factors == NULL) {
				LogMsg ("Memory allocation error\n");
				goto error_exit;
			}
			strcpy (w.known_factors, pkt1.known_factors);
		}

/* Write the exponent to our worktodo file, before acknowledging the assignment with a projected completion date. */

		stop_reason = addWorkToDoLine (tnum, &w, ADD_TO_LOGICAL_END);
		if (stop_reason) goto error_exit;

//bug Can we somehow verify that the worktodo.txt line got written???
//bug (The rogue 'cat' problem)  If not, turn off USE_PRIMENET.
//bug Or rate limit get assignments to N per day (where N takes into account unreserves?)
//bug or have uc return the number of active assignments and ga the capability
//bug to retrieve them

/* Add work unit to our time estimate */

		num_work_units++;
		est = est + work_estimate (tnum, &w);

/* Acknowledge the exponent by sending a projected completion date */

		memset (&pkt2, 0, sizeof (pkt2));
		strcpy (pkt2.computer_guid, COMPUTER_GUID);
		pkt2.cpu_num = tnum;
		strcpy (pkt2.assignment_uid, pkt1.assignment_uid);
		pkt2.pct_complete = 0.0;
		pkt2.end_date = (unsigned long) est;
		pkt2.next_update = (uint32_t) (DAYS_BETWEEN_CHECKINS * 86400.0);
		LOCKED_WORK_UNIT = NULL;
		rc = sendMessage (PRIMENET_ASSIGNMENT_PROGRESS, &pkt2);
		if (rc == PRIMENET_ERROR_INVALID_ASSIGNMENT_KEY) {
			w.assignment_uid[0] = 0;
			updateWorkToDoLine (tnum, &w);
		} else if (rc) {
			goto error_exit;
		}
		talked_to_server = TRUE;
	    }
	}

/* Set some ini flags after we've successfully quit gimps.  It may take a while to get all the proof files sent. */

	char	proof_files[50][255];		// We can send up to 50 proof files
	if ((header_words[1] & HEADER_FLAG_QUIT_GIMPS && USE_PRIMENET) ||
	    (IniGetInt (INI_FILE, KEY_QuitGIMPS, 0) && WORKTODO_COUNT == 0 && ProofFileNames (proof_files) == 0)) {
		USE_PRIMENET = 0;
		IniWriteInt (INI_FILE, "UsePrimenet", 0);
		IniWriteString (INI_FILE, KEY_QuitGIMPS, NULL);
		OutputSomewhere (COMM_THREAD_NUM, "Successfully quit GIMPS.\n");
	}

/* After sending new completion dates remember the current time so that we can send new completion dates in a day. */
/* Set a timer to send them again. */

	else if (header_words[1] & HEADER_FLAG_END_DATES) {
		time_t current_time;
		time (&current_time);
		IniSectionWriteInt (INI_FILE, SEC_Internals, KEY_LastEndDatesSent, (long) current_time);
		if (MANUAL_COMM) delete_timed_event (TE_COMPLETION_DATES);
		add_timed_event (TE_COMPLETION_DATES, (int) (DAYS_BETWEEN_CHECKINS * 86400.0));
	}

/* Delete the spool file.  However, we don't delete the file if any writes */
/* took place after we read the header words.  We detect writes by examining */
/* the first header word. */

	gwmutex_lock (&SPOOL_FILE_MUTEX);
	fd = _open (SPOOL_FILE, _O_RDWR | _O_BINARY);
	if (fd < 0) goto locked_leave;
	_lseek (fd, SPOOL_FILE_HEADER_OFFSET, SEEK_SET);
	read_long (fd, &header_words[0], NULL);
	read_long (fd, &header_words[1], NULL);
	if (header_words[0]) {
		if (header_words[1] & HEADER_FLAG_QUIT_GIMPS)
			header_words[0] |= HEADER_FLAG_QUIT_GIMPS;
		header_words[1] = 0;
		_lseek (fd, SPOOL_FILE_HEADER_OFFSET, SEEK_SET);
		write_long (fd, header_words[0], NULL);
		write_long (fd, header_words[1], NULL);
		_close (fd);
		gwmutex_unlock (&SPOOL_FILE_MUTEX);
		goto retry;
	}
	_close (fd);
	_unlink (SPOOL_FILE);

/* Tell user we're done communicating, then exit this communication thread */

	if (talked_to_server) OutputStr (COMM_THREAD_NUM, "Done communicating with server.\n");
	goto locked_leave;

/* We got an error communicating with the server.  Check for error codes */
/* that require special handling. */

error_exit:

/* If an UPDATE_COMPUTER_INFO command gets an invalid user error, then */
/* the user entered a non-existant userid.  Reset the userid field to the */
/* last value sent to or sent by the server.  This will cause the next uc */
/* command to get the current userid value from the server. */

	if (rc == PRIMENET_ERROR_INVALID_USER) {
		IniSectionGetString (INI_FILE, SEC_Internals, KEY_SrvrUID, USERID, sizeof (USERID), NULL);
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* If an UPDATE_COMPUTER_INFO command gets an cpu identity mismatch error */
/* then the previously registered hardware and windows hashes for this */
/* computer id don't match.  Either the machine hardware has been upgraded */
/* or Windows reinstalled OR the user moved the entire directory to another */
/* machine (a very common way that users install on multiple machines). */
/* First, as a safety measure, reget the computer UID from the INI file in */
/* case it became corrupted in memory.  If no corruption detected then */
/* generate a new computer uid and do an update computer info command again. */

	if (rc == PRIMENET_ERROR_CPU_IDENTITY_MISMATCH) {
		generate_computer_guid ();
//bug - ask user before changing uid?  Delete worktodo? Manipulate computer
// name?
//bug - log a useful message describing error and our remedial action?
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* If the error is STALE_CPU_INFO, then the server is simply */
/* requesting us to do an update computer info again.  Spool the message */
/* then loop to send that message. */

	if (rc == PRIMENET_ERROR_STALE_CPU_INFO) {
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* If the error is UNREGISTERED_CPU, then the COMPUTER_GUID */
/* value is corrupt.  Reget it from the INI file just in case the value */
/* became corrupt in memory.  Otherwise, generate and register a new computer uid */

	if (rc == PRIMENET_ERROR_UNREGISTERED_CPU) {
		// bug
//bug - log a useful message describing error and our remedial action?
		generate_computer_guid ();
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* If the error is CPU_CONFIGURATION_MISMATCH, then the server has */
/* detected an inconsistency in the program options.  Resend them all. */

	if (rc == PRIMENET_ERROR_CPU_CONFIGURATION_MISMATCH) {
		clearCachedProgramOptions ();
		spoolMessage (PRIMENET_UPDATE_COMPUTER_INFO, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* If the error indicates this client is too old, then turn off the */
/* "use primenet" setting until the user upgrades. */

	if (rc == PRIMENET_ERROR_OBSOLETE_CLIENT) {
//bug - delete spool file?NO  write a messsage to screen/log/etc???
//bug - log a useful message describing error and our remedial action?
		OutputStr (MAIN_THREAD_NUM, "Client is obsolete.  Please upgrade.\n");
		obsolete_client = TRUE;
	}

/* If an invalid work preference error is generated (can happen if user */
/* hand edits the prime.txt file) then set the work preference to zero */
/* (get work that makes the most sense) and resend the program options. */
/* As usual, reget the work preference from the ini file just in case */
/* the in-memory copy was inexplicably corrupted. */

	if (rc == PRIMENET_ERROR_INVALID_WORK_TYPE) {
//bug		short	ini_work_preference;
//bug - log a useful message describing error and our remedial action?
//bug		ini_work_preference = (short)
//bug			IniGetInt (INI_FILE, "WorkPreference", 0);
//bug		if (WORK_PREFERENCE != ini_work_preference)
//bug			WORK_PREFERENCE = ini_work_preference;
//bug		else
//bug			WORK_PREFERENCE = 0;
//bug		IniWriteInt (INI_FILE, "WorkPreference", WORK_PREFERENCE);
		spoolMessage (PRIMENET_PROGRAM_OPTIONS, NULL);
		if (retry_count++ < 10) goto retry;
		OutputStr (COMM_THREAD_NUM, RETRY_EXCEEDED);
	}

/* Otherwise, print out message saying where more help may be found. */

	OutputStr (COMM_THREAD_NUM, "Visit http://mersenneforum.org for help.\n");

/* We could not contact the server.  Set up a timer to relaunch the communication thread. */

	if (!MANUAL_COMM) {
		unsigned int retry_time;
		retry_time = (rc == PRIMENET_ERROR_MODEM_OFF) ? MODEM_RETRY_TIME : NETWORK_RETRY_TIME;
		sprintf (buf, "Will try contacting server again in %d %s.\n", retry_time, retry_time == 1 ? "minute" : "minutes");
		OutputStr (COMM_THREAD_NUM, buf);
		ChangeIcon (COMM_THREAD_NUM, IDLE_ICON);
		sprintf (buf, "Waiting %d %s", retry_time, retry_time == 1 ? "minute" : "minutes");
		title (COMM_THREAD_NUM, buf);
		add_timed_event (TE_COMM_SERVER, retry_time * 60);
	}

/* Clear handle indicating communication thread is active.  Return.  This */
/* will end the communication thread. */

leave:
	gwmutex_lock (&SPOOL_FILE_MUTEX);
locked_leave:
	COMMUNICATION_THREAD = 0;
	gwmutex_unlock (&SPOOL_FILE_MUTEX);
	if (!is_timed_event_active (TE_COMM_SERVER)) {
		ChangeIcon (COMM_THREAD_NUM, IDLE_ICON);
		title (COMM_THREAD_NUM, "Inactive");
	}
}

/* This routine tries to salvage information from a corrupt spool file */

void salvageCorruptSpoolFile (void)
{
	int	fd;
	char	filename[128];
	char	inbuf[1000];
	struct primenetAssignmentResult pkt;
	char	*in, *out;
	int	i, j, len, pkts_sent;

/* Output a message */

	OutputBoth (MAIN_THREAD_NUM,
		    "Spool file is corrupt.  Attempting to salvage data.\n");

/* Rename corrupt spool file before extracting data from it */

	strcpy (filename, SPOOL_FILE);
	filename[strlen(filename)-1]++;
	gwmutex_lock (&SPOOL_FILE_MUTEX);
	_unlink (filename);
	rename (SPOOL_FILE, filename);
	gwmutex_unlock (&SPOOL_FILE_MUTEX);

/* Read corrupt file.  Find ASCII characters and send them to the server */
/* in primenetAssignmentResult packets.  Process up to 100000 bytes of */
/* the spool file or 100 lines of output. */

	fd = _open (filename, _O_RDONLY | _O_BINARY);
	if (fd >= 0) {
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		pkt.result_type = PRIMENET_AR_NO_RESULT;
		pkts_sent = 0;
		strcpy (pkt.message, "RECOVERY: ");
		out = pkt.message + 10;
		for (i = 0; i < 100 && pkts_sent < 100; i++) {
			len = _read (fd, &inbuf, sizeof (inbuf));
			if (len <= 0) break;
			for (j = 0, in = inbuf; j < len; j++, in++) {
				if (! isprint (*in) && *in != '\n') continue;
				*out++ = *in;
				if (*in == '\n');
				else if ((int) (out - pkt.message) < sizeof (pkt.message) - 1) continue;
				else *out++ = '\n';
				*out++ = 0;
				if ((int) (out - pkt.message) > 20) {
					spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
					pkts_sent++;
				}
				out = pkt.message + 10;
			}
		}
		_close (fd);
		*out++ = '\n';
		*out++ = 0;
		if ((int) (out - pkt.message) > 20)
			spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* Delete the corrupt spool file */

	_unlink (filename);
}

/****************************************************************************/
/*                        Timed Events Handler                              */
/****************************************************************************/

gwmutex	TIMED_EVENTS_MUTEX;		/* Lock for timed events data */
gwevent TIMED_EVENTS_CHANGED;		/* Signal for telling the scheuler */
					/* there has been a change in the */
					/* array of timed events */
gwthread TIMED_EVENTS_THREAD = 0;	/* Thread for timed events */

struct {
	int	active;			/* TRUE if event is active */
	time_t	time_to_fire;		/* When to start this event */
} timed_events[MAX_TIMED_EVENTS];	/* Array of active timed events */

void timed_events_scheduler (void *arg);


void init_timed_event_handler (void)
{
	int	i;
	gwmutex_init (&TIMED_EVENTS_MUTEX);
	gwevent_init (&TIMED_EVENTS_CHANGED);
	TIMED_EVENTS_THREAD = 0;
	for (i = 0; i < MAX_TIMED_EVENTS; i++)
		timed_events[i].active = FALSE;
}

void add_timed_event (
	int	event_number,		/* Which event to add */
	int	time_to_fire)		/* When to start event (seconds from now) */
{
	time_t	this_time;

	gwmutex_lock (&TIMED_EVENTS_MUTEX);

	time (&this_time);
	if (!timed_events[event_number].active || this_time + time_to_fire < timed_events[event_number].time_to_fire) {
		timed_events[event_number].active = TRUE;
		timed_events[event_number].time_to_fire = this_time + time_to_fire;
	}

	if (!TIMED_EVENTS_THREAD)
		gwthread_create (&TIMED_EVENTS_THREAD, &timed_events_scheduler, NULL);
	else
		gwevent_signal (&TIMED_EVENTS_CHANGED);

	gwmutex_unlock (&TIMED_EVENTS_MUTEX);
}

void delete_timed_event (
	int	event_number)		/* Which event to delete */
{
	gwmutex_lock (&TIMED_EVENTS_MUTEX);
	if (timed_events[event_number].active) {
		timed_events[event_number].active = FALSE;
		gwevent_signal (&TIMED_EVENTS_CHANGED);
	}
	gwmutex_unlock (&TIMED_EVENTS_MUTEX);
}

int is_timed_event_active (
	int	event_number)		/* Which event to test */
{
	return (timed_events[event_number].active);
}

time_t timed_event_fire_time (
	int	event_number)		/* Which event to get fire time of */
{
	return (timed_events[event_number].time_to_fire);
}

void timed_events_scheduler (void *arg)
{
	time_t	this_time;
	int	i;
	time_t	wake_up_time;
	int	there_are_active_events;

/* Loop forever, sleeping until next event fires up */

	for ( ; ; ) {

/* Determine how long until the next timed event */

		gwmutex_lock (&TIMED_EVENTS_MUTEX);
		there_are_active_events = FALSE;
		for (i = 0; i < MAX_TIMED_EVENTS; i++) {
			if (!timed_events[i].active) continue;
			if (!there_are_active_events ||
			    wake_up_time > timed_events[i].time_to_fire)
				wake_up_time = timed_events[i].time_to_fire;
			there_are_active_events = TRUE;
		}
		gwevent_reset (&TIMED_EVENTS_CHANGED);
		gwmutex_unlock (&TIMED_EVENTS_MUTEX);

/* Sleep until we have work to do.  If there are no active events, then */
/* sleep for a million seconds. */

		time (&this_time);
		if (!there_are_active_events) wake_up_time = this_time + 1000000;
		if (wake_up_time > this_time) {
			int	rc;
			rc = gwevent_wait (&TIMED_EVENTS_CHANGED,
					   (int) (wake_up_time - this_time));
			if (rc != GWEVENT_TIMED_OUT) continue;
		}

/* Do action associated with any timed events that have triggered */

		time (&this_time);
		for (i = 0; i < MAX_TIMED_EVENTS; i++) {
			int	fire;
			float	cert_freq;
			gwmutex_lock (&TIMED_EVENTS_MUTEX);
			fire = (timed_events[i].active && this_time >= timed_events[i].time_to_fire);
			gwmutex_unlock (&TIMED_EVENTS_MUTEX);
			if (!fire) continue;
			switch (i) {
			case TE_MEM_CHANGE:	/* Night/day memory change event */
				timed_events[i].active = FALSE;
				mem_settings_have_changed ();
				break;
			case TE_PAUSE_WHILE:	/* Check pause_while_running event */
				timed_events[i].active = FALSE;
				checkPauseWhileRunning ();
				break;
			case TE_WORK_QUEUE_CHECK:	/* Check for CERT work (and regular work) event */
				// Make more powerful computers check for CERT work more frequently (their fair share)
				cert_freq = (float) (HW_NUM_CORES >= 20 ? 3.0 :		/* 20+ cores = 3 hours */
						     HW_NUM_CORES >= 12 ? 4.0 :		/* 12-19 cores = 4 hours */
						     HW_NUM_CORES >= 7 ? 6.0 :		/* 7-11 cores = 6 hours */
						     HW_NUM_CORES >= 3 ? 8.0 :		/* 3-6 cores = 8 hours */
						     HW_NUM_CORES >= 2 ? 12.0 : 24.0);	/* 2 cores = 12 hours, 1 core = 24 hours */
				// Serious CERT volunteers check every half hour
				if (IniGetInt (INI_FILE, "CertDailyCPULimit", 10) >= 50) cert_freq = 0.5;
				// Let user pick their own CERT frequency (in hours).  Minimum is 15 minutes.
				cert_freq = IniGetFloat (INI_FILE, "CertGetFrequency", cert_freq);
				if (cert_freq < 0.25) cert_freq = 0.25;
				// If CERT work is disabled, check regular work queue every 8 hours
				if (!IniGetInt (INI_FILE, "CertWork", 1)) cert_freq = 8.0;
				// Set timer (convert frequency from hours to seconds)
				timed_events[i].time_to_fire = this_time + (int) (cert_freq * 3600.0);
				spoolMessage (MSG_CHECK_WORK_QUEUE, NULL);
				break;
			case TE_COMM_SERVER:	/* Retry communication with server event */
				timed_events[i].active = FALSE;
				gwthread_create (&COMMUNICATION_THREAD, &communicateWithServer, NULL);
				break;
			case TE_COMM_KILL:	/* Kill hung communication thread */
				timed_events[i].active = FALSE;
				GLOBAL_SEND_MSG_COUNT++;
				if (LOCKED_WORK_UNIT != NULL)
					decrementWorkUnitUseCount (LOCKED_WORK_UNIT, SHORT_TERM_USE);
//bug				gwthread_kill (&COMMUNICATION_THREAD);
				break;
			case TE_PRIORITY_WORK:	/* Check for priority work event */
				timed_events[i].time_to_fire = this_time + TE_PRIORITY_WORK_FREQ;
				check_for_priority_work ();
				break;
			case TE_COMPLETION_DATES:	/* Send expected completion dates event */
				timed_events[i].active = FALSE;
				UpdateEndDates ();
				break;
			case TE_THROTTLE:	/* Sleep due to Throttle=n event */
				timed_events[i].time_to_fire = this_time + handleThrottleTimerEvent ();
				break;
			case TE_SAVE_FILES:	/* Timer to trigger writing save files */
						/* Also check for add files */
				timed_events[i].active = FALSE;
				if (addFileExists ()) stop_workers_for_add_files ();	// Look for prime.add
				incorporateWorkToDoAddFile ();				// Append work from worktodo.add
				saveFilesTimer ();
				break;
			case TE_BATTERY_CHECK:	/* Test battery status */
				timed_events[i].time_to_fire = this_time + TE_BATTERY_CHECK_FREQ;
				test_battery ();
				break;
			case TE_ROLLING_AVERAGE: /* Adjust rolling average event */
				timed_events[i].time_to_fire = this_time + TE_ROLLING_AVERAGE_FREQ;
				adjust_rolling_average ();
				break;
			case TE_READ_PAUSE_DATA: /* Reread PauseWhileRunning info */
				timed_events[i].active = FALSE;
				read_pause_info ();
				break;
			case TE_READ_INI_FILE: /* Reread Ini files */
				timed_events[i].active = FALSE;
				stop_workers_for_reread_ini ();
				break;
			case TE_LOAD_AVERAGE:	/* Check load average event */
				timed_events[i].active = FALSE;
				checkLoadAverage ();
				break;
			case TE_BENCH:		/* Benchmark throughput for optimal FFT selection */
				timed_events[i].time_to_fire = this_time + TE_BENCH_FREQ;
				autoBench ();
				break;
			case TE_JACOBI:		/* Timer to trigger Jacobi error checks */
				timed_events[i].active = FALSE;
				JacobiTimer ();
				break;
			}
		}

/* Loop to calculate sleep time until next event */

	}
}

/* Are we in the proof upload time window */

int inUploadWindow (int *minutes_to_start)
{
	char	timebuf[80];
	unsigned int start_time, end_time, current_time;	// In minutes since midnight
	time_t	current_t;
	struct tm *x;

/* Get upload times from INI file */

	IniSectionGetString (INI_FILE, SEC_PrimeNet, KEY_UploadStartTime, timebuf, sizeof (timebuf), "00:00");
	start_time = strToMinutes (timebuf);
	IniSectionGetString (INI_FILE, SEC_PrimeNet, KEY_UploadEndTime, timebuf, sizeof (timebuf), "24:00");
	end_time = strToMinutes (timebuf);

// BUG - Enforce a minimum upload window of 2 hours.  (make this user settable?)
	if ((start_time <= end_time && start_time + 120 > end_time) ||
	    (start_time > end_time && start_time + 120 > end_time + 1440))
		end_time = (start_time + 120) % 1440;

/* Get the current time */

	time (&current_t);
	x = localtime (&current_t);
	current_time = x->tm_hour * 60 + x->tm_min;

/* Return optional minutes until start time begins */

	if (minutes_to_start != NULL)
		*minutes_to_start = (1440 + start_time - current_time) % 1440;

/* Return TRUE if the current time is between start time and end time */

	if (start_time <= end_time) return (current_time >= start_time && current_time <= end_time);
	return (current_time >= start_time || current_time <= end_time);
}

/* This routine runs in its own thread continuously looking for proof files to upload */

void proofUploader (void *arg)
{
	char	proof_files[50][255];		// We can send up to 50 proof files
	int	i, minutes_to_wait, minutes_to_start, num_proof_files;

/* Init event that triggers proof uploads without waiting for next hourly check */

	gwevent_init (&PROOF_UPLOAD_EVENT);

/* Loop forever, waking up once an hour */

	for ( ; ; gwevent_wait (&PROOF_UPLOAD_EVENT, minutes_to_wait * 60)) {

/* Default is to check for proof files once an hour */

		gwevent_reset (&PROOF_UPLOAD_EVENT);
		minutes_to_wait = 60;

/* If we are not using Primenet, wait one hour in case user changes settings from dialog box) */

		if (!USE_PRIMENET) continue;

/* If we are not in the upload time window, pause until we are (or one hour, in case user changes time window from dialog box) */

		if (!inUploadWindow (&minutes_to_start)) {
			ASSERTG (minutes_to_start >= 0);
			if (minutes_to_start < 60) minutes_to_wait = minutes_to_start;
			continue;
		}

/* Start sending all the proof files in the directory */

		// Get the proof files sitting in our directory
		num_proof_files = ProofFileNames (proof_files);
		if (num_proof_files < 0) {
			OutputBoth (COMM_THREAD_NUM, "Unable to get list of proof files in the current directory\n");
			num_proof_files = 0;
		}

		// There is a race condition where this proof uploading thread could attempt to send the proof file
		// before the results reporting thread has submitted the results of the PRP test.  This results in a
		// harmless "Unauthorized" error message.  Here is a rather low-tech "solution" to make this less likely.
		if (num_proof_files) Sleep (60 * 1000);		// Sleep 1 more minute

		// Send each file
		for (i = 0; i < num_proof_files && inUploadWindow (NULL); i++) {
			ProofUpload (proof_files[i]);
		}
	}
}

