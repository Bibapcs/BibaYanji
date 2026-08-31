/*
 * Common routines and variables used by Prime95, Saver95, and NTPrime
 *
 * Comm95a contains information used only during setup
 * Comm95b contains information used only during execution
 * Comm95c contains information used during setup and execution
 *
 * Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
 *
 */ 

/* Common global variables */

#define MAX_THREAD_HANDLES	1024
HANDLE	THREAD_HANDLES[MAX_THREAD_HANDLES] = {0};
int	NUM_THREAD_HANDLES = 0;

/* Common routines */

/* Clear the array of active thread handles */

void clearThreadHandleArray (void)
{
	NUM_THREAD_HANDLES = 0;
}

/* Set the thread priority correctly.  Most screen savers run at priority 4. */
/* Most application's run at priority 9 when in foreground, 7 when in */
/* background.  In selecting the proper thread priority I've assumed the */
/* program usually runs in the background. */ 

void setOsThreadPriority (
	int	priority)		/* Priority, 1=low, 9=high */
{
	HANDLE	h;
	int	pri_class, thread_pri;

/* Get and remember the thread handle */

	h = GetCurrentThread ();
	if (NUM_THREAD_HANDLES < MAX_THREAD_HANDLES)
		THREAD_HANDLES[NUM_THREAD_HANDLES++] = h;

/* Set the thread priority */

	if (priority <= 1) pri_class = NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_IDLE;
	if (priority == 2) pri_class = IDLE_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_LOWEST;
	if (priority == 3) pri_class = IDLE_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_BELOW_NORMAL;
	if (priority == 4) pri_class = BELOW_NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_LOWEST;
	if (priority == 5) pri_class = BELOW_NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_BELOW_NORMAL;
	if (priority == 6) pri_class = NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_LOWEST;
	if (priority == 7) pri_class = NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_BELOW_NORMAL;
	if (priority == 8) pri_class = ABOVE_NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_LOWEST;
	if (priority >= 9) pri_class = ABOVE_NORMAL_PRIORITY_CLASS, thread_pri = THREAD_PRIORITY_BELOW_NORMAL;

	SetPriorityClass (GetCurrentProcess (), pri_class);
	SetThreadPriority (h, thread_pri);

/* Disable thread priority boost */

	SetThreadPriorityBoost (h, TRUE);
}

/* Register a thread termination.  We remove the thread handle from the list of active workers. */

void registerThreadTermination (void)
{
	int	i;
	HANDLE	h;

/* Get the thread handle and remove it from the thread handle array */

	h = GetCurrentThread ();
	for (i = 0; i < NUM_THREAD_HANDLES; i++) {
		if (THREAD_HANDLES[i] != h) break;
		THREAD_HANDLES[i] = THREAD_HANDLES[--NUM_THREAD_HANDLES];
		break;
	}
}

/* When stopping or exiting we raise the priority of all workers so that they can terminate in a timely fashion even if there are other CPU bound tasks running. */

void raiseAllWorkersPriority (void)
{
	int	i;

	SetPriorityClass (GetCurrentProcess (), NORMAL_PRIORITY_CLASS);

/* Loop through the thread handle array raising priority and */
/* setting affinity to run on any CPU. */

	for (i = 0; i < NUM_THREAD_HANDLES; i++) {
		SetThreadPriority (THREAD_HANDLES[i], THREAD_PRIORITY_ABOVE_NORMAL);
		SetThreadAffinityMask (THREAD_HANDLES[i], -1);
	}
}


/* Enumerate processes so that we can pause if a user-specified process */
/* is running.  This code is adapted from Microsoft's knowledge base article */
/* Q175030. */

#if 0 //formerly #ifndef X86_64

#include <psapi.h>
#include <tlhelp32.h>

void checkPauseListCallback (void)
{
	OSVERSIONINFO	osver;
	BOOL		bFlag;
	LPDWORD		lpdwPIDs = NULL;
	DWORD		dwSize, dwSize2, dwIndex;
	char		szFileName[ MAX_PATH ];

// Check to see if were running under Windows95 or Windows NT.

	osver.dwOSVersionInfoSize = sizeof( osver );
	if ( !GetVersionEx( &osver ) ) return;

// If Windows NT:

	if ( osver.dwPlatformId == VER_PLATFORM_WIN32_NT ) {

// Call the PSAPI function EnumProcesses to get all of the
// ProcID's currently in the system.
// NOTE: In the documentation, the third parameter of
// EnumProcesses is named cbNeeded, which implies that you
// can call the function once to find out how much space to
// allocate for a buffer and again to fill the buffer.
// This is not the case. The cbNeeded parameter returns
// the number of PIDs returned, so if your buffer size is
// zero cbNeeded returns zero.
// NOTE: The "HeapAlloc" loop here ensures that we
// actually allocate a buffer large enough for all the
// PIDs in the system.

		dwSize2 = 256 * sizeof (DWORD);
		do {
			if (lpdwPIDs) {
				HeapFree (GetProcessHeap(), 0, lpdwPIDs);
				dwSize2 *= 2;
			}
			lpdwPIDs = (LPDWORD)
				HeapAlloc (GetProcessHeap(), 0, dwSize2);
			if (lpdwPIDs == NULL) goto ntdone;
			if (!EnumProcesses (lpdwPIDs, dwSize2, &dwSize)) goto ntdone;
		} while (dwSize == dwSize2);

// How many ProcID's did we get?

		dwSize /= sizeof (DWORD);

// Loop through each ProcID.

		for (dwIndex = 0; dwIndex < dwSize; dwIndex++) {
			HMODULE		hMod;
			HANDLE		hProcess;

// Open the process (if we can... security does not
// permit every process in the system).

			hProcess = OpenProcess(
				PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
				FALSE, lpdwPIDs[ dwIndex ] );
			if (!hProcess) continue;

// Here we call EnumProcessModules to get only the
// first module in the process this is important,
// because this will be the .EXE module for which we
// will retrieve the full path name in a second.

			if (!EnumProcessModules (hProcess, &hMod, sizeof (hMod), &dwSize2)) {
				CloseHandle (hProcess);
				continue;
			}

// Get Full pathname:

			if (!GetModuleFileNameEx (hProcess, hMod, szFileName, sizeof (szFileName))) {
				CloseHandle (hProcess);
				continue;
			}

			CloseHandle (hProcess);

// Regardless of OpenProcess success or failure, we
// still call the enum func with the ProcID.

			isInPauseList (szFileName);
		}
ntdone:		if (lpdwPIDs) HeapFree (GetProcessHeap(), 0, lpdwPIDs);
		return;
	}

// If Windows 32-bit:

	else if (osver.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
		HANDLE		hSnapShot = NULL;
		PROCESSENTRY32	procentry;

// Get a handle to a Toolhelp snapshot of the systems processes.

		hSnapShot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
		if (hSnapShot == INVALID_HANDLE_VALUE) {
			hSnapShot = NULL;
			goto done95;
		}

// Get the first process' information.

		procentry.dwSize = sizeof (PROCESSENTRY32);
		bFlag = Process32First (hSnapShot, &procentry);

// While there are processes, keep looping.

		while (bFlag) {

// Call the enum func with the filename and ProcID.

			isInPauseList (procentry.szExeFile);

			procentry.dwSize = sizeof(PROCESSENTRY32);
			bFlag = Process32Next (hSnapShot, &procentry);
		}

// Free the library.

done95:		if (hSnapShot) CloseHandle (hSnapShot);
		return;
	}

// Unknown OS type

	return;
}

#else

/* The 64-bit version does not need to dynamically load DLLs. */

#include <tlhelp32.h>
#include <psapi.h>

void checkPauseListCallback (void)
{

// The code below is our second attempt, trying to solve the running as administrator problem
// This gets the name of the executable, but not the full path name.  Thus this is a partial fix
// and we must include both methods.

	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;

// Take a snapshot of all processes in the system.

	hProcessSnap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap != INVALID_HANDLE_VALUE) {

// Retrieve information about the first process, then next processes
// looking for any process that forces us to pause

		pe32.dwSize = sizeof (PROCESSENTRY32);
		if (Process32First (hProcessSnap, &pe32)) do {
			isInPauseList (pe32.szExeFile);
		} while (Process32Next (hProcessSnap, &pe32));

// Cleanup

		CloseHandle( hProcessSnap );
	}

// The code below was our original attempt, but there was a complaint that this will not
// detect some processes running as administrator (not enough privilege to OpenProcess).

	{
	LPDWORD		lpdwPIDs = NULL;
	DWORD		dwSize, dwSize2, dwIndex;
	char		szFileName[ MAX_PATH ];

// Call the PSAPI function EnumProcesses to get all of the
// ProcID's currently in the system.

	dwSize2 = 256 * sizeof (DWORD);
	do {
		if (lpdwPIDs) {
			HeapFree (GetProcessHeap(), 0, lpdwPIDs);
			dwSize2 *= 2;
		}
		lpdwPIDs = (LPDWORD)
			HeapAlloc (GetProcessHeap(), 0, dwSize2);
		if (lpdwPIDs == NULL) goto ntdone;
		if (!EnumProcesses (lpdwPIDs, dwSize2, &dwSize))
			goto ntdone;
	} while (dwSize == dwSize2);

// How many ProcID's did we get?

	dwSize /= sizeof (DWORD);

// Loop through each ProcID.

	for (dwIndex = 0; dwIndex < dwSize; dwIndex++) {
		HMODULE	hMod;
		HANDLE	hProcess;

// Open the process (if we can... security does not
// permit every process in the system).

		hProcess = OpenProcess (
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
			FALSE, lpdwPIDs[ dwIndex ] );
		if (!hProcess) continue;

// Here we call EnumProcessModules to get only the
// first module in the process this is important,
// because this will be the .EXE module for which we
// will retrieve the full path name in a second.

		if (!EnumProcessModules (hProcess, &hMod, sizeof (hMod), &dwSize2)) {
			CloseHandle (hProcess);
			continue;
		}

// Get Full pathname:

		if (!GetModuleFileNameEx (hProcess, hMod, szFileName, sizeof (szFileName))) {
			CloseHandle (hProcess);
			continue;
		}

		CloseHandle (hProcess);

// Does this process force us to pause?

		isInPauseList (szFileName);
	}
ntdone:	if (lpdwPIDs) HeapFree (GetProcessHeap(), 0, lpdwPIDs);
	}
}

#endif

/* Get the load average - Windows does not support this */

double get_load_average (void)
{
	return (-1.0);
}

/* Tell malloc to free memory back to the OS */

void mallocFreeForOS () {
}

