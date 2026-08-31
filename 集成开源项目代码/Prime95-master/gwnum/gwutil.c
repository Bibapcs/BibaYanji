/*----------------------------------------------------------------------
| gwutil.c
|
| This file contains various utility routines that may be used by gwnum
| routines, prime95, or other consumers of gwnum.
| 
|  Copyright 2004-2020 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

/* Include files */

#include <stdlib.h>
#include <memory.h>
#include <string.h>
#ifdef _WIN32
#include "windows.h"
#endif
#include <stdlib.h>
#if defined (__linux__) || defined (__HAIKU__)
#include <malloc.h>
#include <sys/mman.h>
#endif
#if defined (__APPLE__)
#include <sys/mman.h>
#include <mach/vm_statistics.h>
#endif
#if defined (__FreeBSD__)
#include <sys/mman.h>
#endif
#include "gwcommon.h"
#include "gwutil.h"


/* Aligned malloc routines.  MSVC 8 supports these in the C runtime library. */
/* Emulate these routines for other ports. */

void * aligned_offset_malloc (
	size_t	size,
	size_t	alignment,
	size_t	mod)
{
// For portability, I've elected to use my own implementation rather than the Microsoft routine below.
//	return (_aligned_offset_malloc (size, alignment, mod));
	char	*p, *q;
	p = (char *) malloc (sizeof (void *) + size + alignment);
	if (p == NULL) return (NULL);
	q = (char *) (((intptr_t) p + sizeof (void *) + mod + alignment - 1) & ~(alignment - 1)) - mod;
	* (void **) ((char *) q - sizeof (void *)) = p;
	return (q);
}

void * aligned_malloc (
	size_t	size,
	size_t	alignment)
{
// For portability, I've elected to use my own implementation rather than the Microsoft routine below.
//	return (_aligned_malloc (size, alignment));
	return (aligned_offset_malloc (size, alignment, 0));
}

void aligned_free (
	void	*ptr)
{
// For portability, I've elected to use my own implementation rather than the Microsoft routine below.
//	_aligned_free (ptr);
	if (ptr == NULL) return;
	free (* (void **) ((char *) ptr - sizeof (void *)));
}


//*******************************************************
//           Large/Huge/Super Page routines
//*******************************************************

#define TWO_MEGABYTES	2*1024*1024

static int large_pages_are_supported = 0;
#if defined (_WIN32)
static size_t windows_large_page_size = 0;
#endif

// Internal routine to do any required one-time initialization

void large_pages_init ()
{
static	int large_pages_first_call = 1;
	void	*p;

// Return if init has already been done

	if (!large_pages_first_call) return;
	large_pages_first_call = 0;

// Assume large pages are supported unless we learn otherwise (a failed large_pages_malloc call)

	large_pages_are_supported = TRUE;

// Jean Penne reports that MSVC6 does not define MEM_LARGE_PAGES.
// Simple fix - we don't support large pages for older compilers.

#if defined (_WIN32) && defined (MEM_LARGE_PAGES)
	{
	DWORD	lasterr;
	HANDLE	hToken;
	LUID	luid;
	TOKEN_PRIVILEGES tp;
	HINSTANCE hDll;      
	int (*pGetLargePageMinimum)(void);

	// Grant large page access
	OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken);
	lasterr = GetLastError ();
	LookupPrivilegeValue (NULL, "SeLockMemoryPrivilege", &luid);
	lasterr = GetLastError ();

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	AdjustTokenPrivileges (hToken, FALSE, &tp, sizeof (TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES) NULL, (PDWORD) NULL);
	lasterr = GetLastError ();

	// Dynamic link to get large page size
	// Call succeeds only on Windows Server 2003 SP1 or later
	hDll = LoadLibrary (TEXT ("kernel32.dll"));
	pGetLargePageMinimum = (int (*)(void)) GetProcAddress (hDll, "GetLargePageMinimum");
	if (pGetLargePageMinimum != NULL) windows_large_page_size = (*pGetLargePageMinimum)();
	FreeLibrary (hDll);

	// To prevent locking down too much memory (like 1GB), only use large pages if the large page size is 2MB or 4MB
	large_pages_are_supported = (windows_large_page_size == TWO_MEGABYTES || windows_large_page_size == 2*TWO_MEGABYTES);
	}
#endif

// Try allocating a large page, if it fails set flag indicating large pages are not supported

	p = large_pages_malloc (TWO_MEGABYTES/2);
	if (p == NULL) large_pages_are_supported = FALSE;
	else large_pages_free (p);
}

// Return TRUE if the OS can allocate 2MB large pages

int large_pages_supported ()
{
	large_pages_init ();
	return (large_pages_are_supported);
}

// Allocate a block of memory using large pages

void * large_pages_malloc (
	size_t	size)
{
	void	*p;
	uint64_t *q;

// Initialize large pages.  If this system does not support large pages, return NULL.

	large_pages_init ();
	if (!large_pages_are_supported) return (NULL);

// On Linux, we need to write the length before the returned address for free to work.  On Windows, we need to remember the size
// so that we can keep track of the total amount of large page memory we have currently allocated.  Allocate an extra 4KB so that
// caller can generate an address that is on a 4KB boundary.

	size = round_up_to_multiple_of (size + 4096, TWO_MEGABYTES);

// Now allocate the memory

#if defined (_WIN32) && defined (MEM_LARGE_PAGES)
	{
	DWORD lasterr;
	size = round_up_to_multiple_of (size, windows_large_page_size);
	p = VirtualAlloc (NULL, size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
	lasterr = GetLastError ();
	if (p == NULL) return (NULL);
	}
#elif defined (__linux__)
	p = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (p == MAP_FAILED) return (NULL);
#elif defined (__APPLE__)
	p = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0);
	if (p == MAP_FAILED) return (NULL);
#elif defined (__FreeBSD__)
	p = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_ALIGNED_SUPER, -1, 0);
	if (p == MAP_FAILED) return (NULL);
#else
	return (NULL);
#endif

// Write the length before the pointer we are returning

	q = (uint64_t *) p;
	*q++ = size;
	return (q);
}

void large_pages_free (
	void	*ptr)
{
	uint64_t *q, size;

// Ignore NULL frees

	if (ptr == NULL) return;

// The length was written in the 64-bits in front of the pointer

	q = (uint64_t *) ptr;
	size = *--q;

//BUG - keep track of total mem allocated

#ifdef _WIN32
	VirtualFree (q, 0, MEM_RELEASE);
#elif defined (__linux__) || defined (__APPLE__) || defined (__FreeBSD__)
	munmap (q, size);
#endif
}

/* Large page versions of the aligned malloc routines */

void * aligned_offset_large_pages_malloc (
	size_t	size,
	size_t	alignment,
	size_t	mod)
{
	char	*p, *q;
	p = (char *) large_pages_malloc (sizeof (void *) + size + alignment);
	if (p == NULL) return (NULL);
	q = (char *) (((intptr_t) p + sizeof (void *) + mod + alignment - 1) & ~(alignment - 1)) - mod;
	* (void **) ((char *) q - sizeof (void *)) = p;
	return (q);
}

void * aligned_large_pages_malloc (
	size_t	size,
	size_t	alignment)
{
	return (aligned_offset_large_pages_malloc (size, alignment, 0));
}

void aligned_large_pages_free (
	void	*ptr)
{
	if (ptr == NULL) return;
	large_pages_free (* (void **) ((char *) ptr - sizeof (void *)));
}


//*******************************************************
//       Utility routines used in copying strings
//*******************************************************

void truncated_strcpy_with_len (
	char	*buf,
	unsigned int bufsize,
	const char *val,
	unsigned int valsize)
{
	if (valsize >= bufsize) valsize = bufsize - 1;
	memcpy (buf, val, valsize);
	buf[valsize] = 0;
}

void truncated_strcpy (
	char	*buf,
	unsigned int bufsize,
	const char *val)
{
	truncated_strcpy_with_len (buf, bufsize, val, (unsigned int) strlen (val));
}

/* Utility time routine available in Linux but not Windows */

#ifdef _WIN32
int gettimeofday (
	struct timeval *tp,
	void	*tzp)
{
	// This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
	// until 00:00:00 January 1, 1970 
	static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	uint64_t    time;

	GetSystemTime (&system_time);
	SystemTimeToFileTime (&system_time, &file_time);
	time =  ((uint64_t) file_time.dwLowDateTime);
	time += ((uint64_t) file_time.dwHighDateTime) << 32;

	tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
	tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
	return 0;
}
#endif

