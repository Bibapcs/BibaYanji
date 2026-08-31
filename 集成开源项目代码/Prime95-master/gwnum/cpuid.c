/*----------------------------------------------------------------------
| Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
| Author:  George Woltman
| Email: woltman@alum.mit.edu
|
| This file contains routines to determine the CPU type and speed.
+---------------------------------------------------------------------*/

/* Include files */

#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#ifdef _WIN32
#include "windows.h"
#endif
#if defined (__FreeBSD__) || defined (__APPLE__)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#ifdef __linux__
#include <stdio.h>
#endif
#ifdef __OS2__
#define INCL_DOSPROFILE
#include <os2.h>
#endif
#if defined (__HAIKU__)
#include <unistd.h>
#endif
#if defined (__linux__) || defined (__FreeBSD__) || defined (__APPLE__) || defined (__HAIKU__) || defined (__EMX__)
#include <sys/time.h>
#endif
#include "cpuid.h"
#include "gwthread.h"
#include "gwutil.h"

#define safe_strcpy(d,s)	memmove (d, s, strlen (s) + 1)

/* Global variables describing the CPU we are running on */

char	CPU_BRAND[49] = "";
double	CPU_SPEED = 0.0;
unsigned int CPU_FLAGS = 0;
unsigned int CPU_CORES = 1;		/* Number CPU cores */
unsigned int CPU_HYPERTHREADS = 1;	/* Number of virtual processors */
					/* that each CPU core supports. */
					/* Total number logical processors */
					/* is CPU_CORES * CPU_HYPERTHREADS */
int	CPU_L1_CACHE_SIZE = -1;
int	CPU_L2_CACHE_SIZE = -1;
int	CPU_L3_CACHE_SIZE = -1;
int	CPU_L1_CACHE_LINE_SIZE = -1;
int	CPU_L2_CACHE_LINE_SIZE = -1;
int	CPU_L3_CACHE_LINE_SIZE = -1;
int	CPU_L1_DATA_TLBS = -1;
int	CPU_L2_DATA_TLBS = -1;
int	CPU_L3_DATA_TLBS = -1;
int	CPU_L1_SET_ASSOCIATIVE = -1;
int	CPU_L2_SET_ASSOCIATIVE = -1;
int	CPU_L3_SET_ASSOCIATIVE = -1;

unsigned int CPU_SIGNATURE = 0;		/* Vendor-specific family number, */
					/* model number, stepping ID, etc. */

int	CPU_ARCHITECTURE = 0;		/* Our attempt to derive the CPU */
					/* architecture. */

/* Masm routines to burn up a specific number of clocks */

void one_hundred_thousand_clocks_help ();
void one_million_clocks_help ();

/* Routines to burn up a specific number of clocks */

void one_hundred_thousand_clocks (void)
{
	/* The helper function times dependent ROR instructions which have a throughput of 1 clock on Intel and AMD. */
	/* If a future chip, needs to call a different helper routine, we'd make that choice here. */
	one_hundred_thousand_clocks_help ();
}

void one_million_clocks (void)
{
	/* The helper function times dependent ROR instructions which have a throughput of 1 clock on Intel and AMD. */
	/* If a future chip, needs to call a different helper routine, we'd make that choice here. */
	one_million_clocks_help ();
}

/* Return the number of CPUs in the system */

unsigned int num_cpus (void)
{
#if defined (_WIN32)
	SYSTEM_INFO sys;

	GetSystemInfo (&sys);
	return (sys.dwNumberOfProcessors);
#elif defined (__APPLE__) || defined (__FreeBSD__)
	int	mib[2];
	int	ncpus;
	size_t	len;

	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	len = sizeof (ncpus);
	sysctl (mib, 2, &ncpus, &len, NULL, 0);
	return (ncpus);
#elif defined (__linux__)
	FILE	*fd;
	char	buf[200];
	int	count;

	count = 0;
	fd = fopen ("/proc/cpuinfo", "r");
	if (fd != NULL) {
		while (fgets (buf, sizeof (buf), fd) != NULL) {
			buf[9] = 0;
			if (strcmp (buf, "processor") == 0) count++;
		}
		fclose (fd);
	}
	if (count == 0) count = 1;
	return (count);
#elif defined (__HAIKU__)
	int	ncpus;

	ncpus = sysconf(_SC_NPROCESSORS_CONF);
	return (ncpus);
#else
	return (1);
#endif
}

/* The MS 64-bit compiler does not allow inline assembly.  Fortunately, any */
/* CPU capable of running x86-64 bit code can execute these instructions. */

#ifdef X86_64

int canExecInstruction (
	unsigned long cpu_flag)
{
	return (TRUE);
}

#else

/* Internal routines to see if CPU-specific instructions (RDTSC, CMOV */
/* SSE, SSE2) are supported.  CPUID could report them as supported yet */
/* the OS might not support them.   Use signals in non-MSVC compiles. */

#if !defined(_MSC_VER) && !defined(__WATCOMC__)

#include <setjmp.h>
#include <signal.h>
int	boom;
jmp_buf	env;
void sigboom_handler (int i)
{
	boom = TRUE;
	longjmp (env, 1);
}
int canExecInstruction (
	unsigned long cpu_flag)
{
	boom = FALSE;
	(void) signal (SIGILL, sigboom_handler);
	if (setjmp (env) == 0) {
		switch (cpu_flag) {
		case CPU_RDTSC:		/* RDTSC */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0x31\n");
			break;
		case CPU_CMOV:		/* CMOV */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0x42\n .byte 0xC0\n");
			break;
/* WARNING: On Mac OS X 64-bit executable, executing this PADDB instruction will cause */
/* the next call to log in math.h to return NaN.  Submitted bug to Apple: Bug ID# 6478193. */
		case CPU_MMX:		/* PADDB */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0xFC\n .byte 0xC0\n");
			break;
		case CPU_SSE:		/* ORPS */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0x56\n .byte 0xC0\n");
			break;
		case CPU_SSE2:		/* ADDPD */
			__asm__ __volatile__ (".byte 0x66\n .byte 0x0F\n .byte 0x58\n .byte 0xC0\n");
			break;
		case CPU_PREFETCH:	/* PREFETCHT1 */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0x18\n .byte 0x16\n");
			break;
		case CPU_3DNOW:		/* PREFETCHW */
			__asm__ __volatile__ (".byte 0x0F\n .byte 0x0D\n .byte 0x0E\n");
			break;
		}
	}
	(void) signal (SIGILL, SIG_DFL);
	fpu_init ();
	return (!boom);
}

#else

#include <excpt.h>
#ifdef __WATCOMC__
#define __asm	_asm
#define __emit	db
#endif

int canExecInstruction (
	unsigned long cpu_flag)
{
	int	succeeded;
	__try {
		switch (cpu_flag) {
		case CPU_RDTSC:		/* RDTSC */
			__asm __emit 0x0F
			__asm __emit 0x31
			break;
		case CPU_CMOV:		/* CMOV */
			__asm __emit 0x0F
			__asm __emit 0x42
			__asm __emit 0xC0
			break;
		case CPU_MMX:		/* PADDB */
			__asm __emit 0x0F
			__asm __emit 0xFC
			__asm __emit 0xC0
			break;
		case CPU_SSE:		/* ORPS */
			__asm __emit 0x0F
			__asm __emit 0x56
			__asm __emit 0xC0
			break;
		case CPU_SSE2:		/* ADDPD */
			__asm __emit 0x66
			__asm __emit 0x0F
			__asm __emit 0x58
			__asm __emit 0xC0
			break;
		case CPU_PREFETCH:	/* PREFETCHT1 */
			__asm __emit 0x0F
			__asm __emit 0x18
			__asm __emit 0x16
			break;
		case CPU_3DNOW:		/* PREFETCHW */
			__asm __emit 0x0F
			__asm __emit 0x0D
			__asm __emit 0x0E
			break;
		}
		succeeded = TRUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		succeeded = FALSE;
	}
	fpu_init ();
	return (succeeded);
}
#endif
#endif

/* Busy loop to keep CPU cores occupied.  Used sometimes to aid */
/* in measuring CPU speed. */

int	end_busy_loop = FALSE;

void busy_loop (void *arg)
{
	while (!end_busy_loop) one_million_clocks ();
}

/* Work with CPUID instruction to guess the cpu type and features */
/* See Intel's document AP-485 for using CPUID on Intel processors */
/* AMD and VIA have similar documents */

void guessCpuType (void)
{
	struct cpuid_data reg;
	unsigned int num_logical_processors;
	unsigned long max_cpuid_value;
	unsigned long max_extended_cpuid_value;
	unsigned long extended_family, extended_model, type, family_code;
	unsigned long model_number, stepping_id, brand_index;
	unsigned long family, model, retry_count;
	char	vendor_id[13];
static	char *	BRAND_NAMES[] = {	/* From Intel Ap-485 */
			"",	/* brand_index = 0 */
			"Intel(R) Celeron(R) processor",
			"Intel(R) Pentium(R) III processor",
			"Intel(R) Pentium(R) III Xeon(TM) processor",
			"Intel(R) Pentium(R) III processor",
			"",	/* brand_index = 5 */
			"Mobile Intel(R) Pentium(R) III Processor - M",
			"Mobile Intel(R) Celeron(R) processor",
			"Intel(R) Pentium(R) 4 processor",
			"Intel(R) Pentium(R) 4 processor",
			"Intel(R) Celeron(R) processor",
			"Intel(R) Xeon(TM) processor",
			"Intel(R) Xeon(TM) Processor MP",
			"",	/* brand_index = 13 */
			"Mobile Intel(R) Pentium(R) 4 Processor - M",
			"Mobile Intel(R) Celeron(R) processor",
			"",	/* brand_index = 16 */
			"Mobile Intel(R) processor",
			"Mobile Intel(R) Celeron(R) M processor",
			"Mobile Intel(R) Celeron(R) processor",
			"Intel(R) Celeron(R) processor",
			"Mobile Intel(R) processor",
			"Intel(R) Pentium(R) M processor"
			"Mobile Intel(R) Celeron(R) processor"
	};
#define NUM_BRAND_NAMES	(sizeof (BRAND_NAMES) / sizeof (char *))

/* Get the number of logical processors */

	num_logical_processors = num_cpus ();

/* Set up default values for features we cannot determine with CPUID */

	CPU_BRAND[0] = 0;
	CPU_SPEED = 100.0;
	CPU_FLAGS = 0;
	CPU_CORES = 1;
	CPU_HYPERTHREADS = 1;
	CPU_L1_CACHE_SIZE = -1;
	CPU_L2_CACHE_SIZE = -1;
	CPU_L3_CACHE_SIZE = -1;
	CPU_L1_CACHE_LINE_SIZE = -1;
	CPU_L2_CACHE_LINE_SIZE = -1;
	CPU_L3_CACHE_LINE_SIZE = -1;
	CPU_L1_DATA_TLBS = -1;
	CPU_L2_DATA_TLBS = -1;
	CPU_L3_DATA_TLBS = -1;
	CPU_SIGNATURE = 0;

/* If CPUID instruction is not supported, assume we have a 486 (not all */
/* 486 chips supported CPUID.  The CPU might be something else, but that */
/* isn't particularly important. */

	if (! isCpuidSupported ()) {
		strcpy (CPU_BRAND, "CPUID not supported - 486 CPU assumed");
		return;
	}

/* Call CPUID with 0 argument.  It returns how highest argument CPUID */
/* can accept as well as the vendor string */

	Cpuid (0, &reg);
	max_cpuid_value = reg.EAX;
	memcpy (vendor_id, &reg.EBX, 4);
	memcpy (vendor_id+4, &reg.EDX, 4);
	memcpy (vendor_id+8, &reg.ECX, 4);
	vendor_id[12] = 0;

/* So far all vendors have adopted Intel's definition of CPUID with 1 as an */
/* argument.  Let's assume future vendors will do the same.  CPUID returns */
/* the processor family, stepping, etc.  It also returns the feature flags. */

	if (max_cpuid_value >= 1) {
		Cpuid (1, &reg);
		CPU_SIGNATURE = reg.EAX & 0x0FFF3FFF;
		extended_family = (reg.EAX >> 20) & 0xFF;
		extended_model = (reg.EAX >> 16) & 0xF;
		type = (reg.EAX >> 12) & 0x3;
		family_code = (reg.EAX >> 8) & 0xF;
		model_number = (reg.EAX >> 4) & 0xF;
		stepping_id = reg.EAX & 0xF;
		brand_index = reg.EBX & 0xFF;
		if ((reg.EDX >> 4) & 0x1 && canExecInstruction (CPU_RDTSC))
			CPU_FLAGS |= CPU_RDTSC;
		if ((reg.EDX >> 15) & 0x1 && canExecInstruction (CPU_CMOV))
			CPU_FLAGS |= CPU_CMOV;
		if ((reg.EDX >> 23) & 0x1 && canExecInstruction (CPU_MMX))
			CPU_FLAGS |= CPU_MMX;
		if ((reg.EDX >> 25) & 0x1 && canExecInstruction (CPU_PREFETCH))
			CPU_FLAGS |= CPU_PREFETCH;
		if ((reg.EDX >> 25) & 0x1 && canExecInstruction (CPU_SSE))
			CPU_FLAGS |= CPU_SSE;
		if ((reg.EDX >> 26) & 0x1 && canExecInstruction (CPU_SSE2))
			CPU_FLAGS |= CPU_SSE2;
		if ((reg.ECX >> 0) & 0x1)
			CPU_FLAGS |= CPU_SSE3;
		if ((reg.ECX >> 9) & 0x1)
			CPU_FLAGS |= CPU_SSSE3;
		if ((reg.ECX >> 19) & 0x1)
			CPU_FLAGS |= CPU_SSE41;
		if ((reg.ECX >> 20) & 0x1)
			CPU_FLAGS |= CPU_SSE42;

/* If hardware supports AVX that doesn't mean the OS supports AVX. */
/* See if OS supports XGETBV, then see if OS supports AVX, FMA, and AVX-512. */

		if ((reg.ECX >> 27) & 0x1) {
			struct cpuid_data getbv_reg;
			Xgetbv (0, &getbv_reg);

			if (((reg.ECX >> 28) & 0x1) && ((getbv_reg.EAX & 6) == 6)) CPU_FLAGS |= CPU_AVX;
			if (((reg.ECX >> 12) & 0x1) && (CPU_FLAGS & CPU_AVX)) CPU_FLAGS |= CPU_FMA3;

/* Get more feature flags.  Specifically the AVX2, AVX512F, AVX512PF and PREFETCHWT1 flags. */

			if (max_cpuid_value >= 7) {
				reg.ECX = 0;
				Cpuid (7, &reg);
				if (((reg.EBX >> 5) & 0x1) && (CPU_FLAGS & CPU_AVX)) CPU_FLAGS |= CPU_AVX2;
				if (((reg.EBX >> 16) & 0x1) && ((getbv_reg.EAX & 0xE0) == 0xE0)) CPU_FLAGS |= CPU_AVX512F;
				if (((reg.EBX >> 26) & 0x1) && (CPU_FLAGS & CPU_AVX512F)) CPU_FLAGS |= CPU_AVX512PF;
				if (reg.ECX & 0x1) CPU_FLAGS |= CPU_PREFETCHWT1;
			}
		}
	}

/* Call CPUID with 0x80000000 argument.  It tells us how many extended CPU */
/* functions are supported. */

	Cpuid (0x80000000, &reg);
	max_extended_cpuid_value = reg.EAX;

/* Get more feature flags.  Specifically the PREFETCHW flag and the flag that says */
/* RDTSC counts independently of CPU clock ticks Intel did this so that RDTSC */
/* would keep accurate real time regardless of the CPU core speed controlled by SpeedStep. */

	if (max_extended_cpuid_value >= 0x80000001) {
		reg.ECX = 0;
		Cpuid (0x80000001, &reg);
		if ((reg.ECX >> 8) & 0x1)
			CPU_FLAGS |= CPU_PREFETCHW;
	}
	if (max_extended_cpuid_value >= 0x80000007) {
		Cpuid (0x80000007, &reg);
		if ((reg.EDX >> 8) & 0x1)
			CPU_FLAGS |= CPU_TSC_INVARIANT;
	}

/* Two users on the Mersenne forums have reported their Core 2 machines */
/* are not getting the brand string right after a reboot.  How strange. */
/* The only way I can see this happening is if CPUID returns the wrong */
/* max_extended_cpuid_value or an empty brand string.  In any attempt to */
/* "fix" this, I'll retry getting the brand string several times. */
/* Lookup Intel errata AZ64 in Core 45 nm specification. */

	for (retry_count = 0; retry_count < 100; retry_count++) {

/* Although not guaranteed, all vendors have standardized on putting the */
/* brand string (if supported) at cpuid calls 0x8000002, 0x80000003, and */
/* 0x80000004.  We'll assume future vendors will do the same. */

		if (max_extended_cpuid_value >= 0x80000004) {
			Cpuid (0x80000002, &reg);
			memcpy (CPU_BRAND, &reg.EAX, 4);
			memcpy (CPU_BRAND+4, &reg.EBX, 4);
			memcpy (CPU_BRAND+8, &reg.ECX, 4);
			memcpy (CPU_BRAND+12, &reg.EDX, 4);
			Cpuid (0x80000003, &reg);
			memcpy (CPU_BRAND+16, &reg.EAX, 4);
			memcpy (CPU_BRAND+20, &reg.EBX, 4);
			memcpy (CPU_BRAND+24, &reg.ECX, 4);
			memcpy (CPU_BRAND+28, &reg.EDX, 4);
			Cpuid (0x80000004, &reg);
			memcpy (CPU_BRAND+32, &reg.EAX, 4);
			memcpy (CPU_BRAND+36, &reg.EBX, 4);
			memcpy (CPU_BRAND+40, &reg.ECX, 4);
			memcpy (CPU_BRAND+44, &reg.EDX, 4);
			CPU_BRAND[48] = 0;
			while (CPU_BRAND[0] == ' ') safe_strcpy (CPU_BRAND, CPU_BRAND+1);
		}

/* If we got the brand string, or this is a very old CPU that perhaps */
/* doesn't support the brand string, then break the retry loop. */

		if (CPU_BRAND[0] || ! (CPU_FLAGS & CPU_SSE2)) break;
		if (max_extended_cpuid_value < 0x80000004)
			max_extended_cpuid_value = 0x80000004;
	}

/*-------------------------------------------------------------------+
| Check for INTEL vendor string.  Perform INTEL-specific operations. |
+-------------------------------------------------------------------*/

	if (strcmp ((const char *) vendor_id, "GenuineIntel") == 0) {

/* Intel wants us to start paying attention to extended family */
/* and extended model numbers.  The AP-485 document isn't clear whether */
/* this should always be done (the documentation) or just done for */
/* families 6 and 15 (the sample code). */

		if (family_code == 15)
			family = extended_family + family_code;
		else
			family = family_code;
		if (family_code == 15 || family_code == 6)
			model = (extended_model << 4) + model_number;
		else
			model = model_number;

/* According to "Intel 64 and IA-32 Architectures Optimization Reference */
/* Manual", only early Pentium 4's require TLB priming during prefetch. */

		if (family_code == 15 && model_number <= 2)
			CPU_FLAGS |= CPU_TLB_PRIMING;

/* Try to determine the CPU architecture.  See https://en.wikichip.org/wiki/intel/cpuid#Family_15 */

		if (! (CPU_FLAGS & CPU_SSE2))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PRE_SSE2;
		else if ((family == 15 && model <= 4) ||
			 (family == 15 && model == 6))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PENTIUM_4;
		else if ((family == 6 && model == 9) ||
			 (family == 6 && model == 13))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PENTIUM_M;
		else if (family == 6 && model == 14)
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_CORE;
		else if ((family == 6 && model == 15) ||
			 (family == 6 && model == 22) ||
			 (family == 6 && model == 23) ||
			 (family == 6 && model == 29))			// Xeon MP (based on Core 2 technology)
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_CORE_2;
		else if ((family == 6 && model == 26) ||		// Core i7
			 (family == 6 && model == 30) ||		// Core i5/i7
			 (family == 6 && model == 46) ||		// Xeon MP (based on Core i7 technology)
			 (family == 6 && model == 47) ||		// Xeon MP (based on Sandy Bridge technology)
			 (family == 6 && model == 69) ||		// Xeon MP (based on Haswell technology)
			 (family == 6 && model == 44) ||		// Core i7 (based on Sandy Bridge technology)
			 (family == 6 && model == 37) ||		// Core i3, mobile i5/i7 (based on Sandy Bridge technology)
			 (family == 6 && model == 42) ||		// Core i7 (based on Sandy Bridge technology)
			 (family == 6 && model == 45) ||		// Core i7 (based on Sandy Bridge-E technology)
			 (family == 6 && model == 58) ||		// Core i7 (based on Ivy Bridge technology)
			 (family == 6 && model == 62) ||		// Core i7 (based on Ivy Bridge-E technology)
			 (family == 6 && model == 60) ||		// Core i7 (based on Haswell technology)
			 (family == 6 && model == 63) ||		// Core i7 (based on Haswell-E technology)
			 (family == 6 && model == 69) ||		// Core i7, mobile (based on Haswell technology)
			 (family == 6 && model == 70) ||		// Core i7 (based on Haswell technology)
			 (family == 6 && model == 61) ||		// Core i7 (based on Broadwell technology)
			 (family == 6 && model == 79) ||		// Core i7 (based on Broadwell-E technology)
			 (family == 6 && model == 71) ||		// Core i7, mobile (based on Broadwell technology)
			 (family == 6 && model == 85) ||		// Core i9 (based on Skylake-X technology)
			 (family == 6 && model == 86) ||		// Core i7, mobile (based on Broadwell technology)
			 (family == 6 && model == 94) ||		// Core i7 (based on Skylake technology)
			 (family == 6 && model == 142) ||		// Core i7, (based on Kaby Lake technology)
			 (family == 6 && model == 158) ||		// Core i7, mobile (based on Coffee Lake technology)
			 (family == 6 && model == 168) ||		// Core i7, (based on Coffee Lake technology)
			 (family == 6 && model == 78) ||		// Core i3/i5/i7, mobile (based on Skylake technology)
			 (family == 6 && model == 102) ||		// Core i3/i5/i7, Cannon Lake
			 (family == 6 && model == 165) ||		// Core i3/i5/i7, Comet Lake
			 (family == 6 && model == 106) ||		// Core i3/i5/i7, Ice Lake
			 (family == 6 && model == 108) ||		// Core i3/i5/i7, Ice Lake
			 (family == 6 && model == 125) ||		// Core i3/i5/i7, Ice Lake
			 (family == 6 && model == 126) ||		// Core i3/i5/i7, Ice Lake
			 (family == 6 && model == 143) ||		// Core i3/i5/i7, Sapphire Rapids
			 (family == 6 && model == 140) ||		// Core i3/i5/i7, Tiger Lake
			 (family == 6 && model == 141) ||		// Core i3/i5/i7, Tiger Lake
			 (family == 6 && model == 167) ||		// Core i3/i5/i7, Rocket Lake
			 (family == 6 && model == 154) ||		// Core i3/i5/i7, Alder Lake
			 (family == 6 && model == 151) ||		// Core i3/i5/i7, Alder Lake
			 (family == 6 && model == 183) ||		// Core i3/i5/i7/i9, Raptor Lake
			 (family == 6 && model == 186))			// Core i3/i5/i7/i9, Raptor Lake
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_CORE_I7;
		else if ((family == 6 && model == 28) ||
			 (family == 6 && model == 38) ||
			 (family == 6 && model == 39) ||
			 (family == 6 && model == 53) ||
			 (family == 6 && model == 54) ||
			 (family == 6 && model == 55) ||
			 (family == 6 && model == 74) ||		// Silvermont
			 (family == 6 && model == 77) ||
			 (family == 6 && model == 90) ||
			 (family == 6 && model == 93) ||
			 (family == 6 && model == 76) ||		// Airmont
			 (family == 6 && model == 92) ||		// Goldmont
			 (family == 6 && model == 95) ||
			 (family == 6 && model == 122) ||		// Goldmont+
			 (family == 6 && model == 134) ||		// Tremont
			 (family == 6 && model == 138) ||
			 (family == 6 && model == 150) ||
			 (family == 6 && model == 156))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_ATOM;
		else if ((family == 6 && model == 87) ||		// Knight's Landing
			 (family == 6 && model == 133))			// Knight's Mill
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PHI;
		else
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_INTEL_OTHER;

/* Try to determine if hyperthreading is supported.  I think this code */
/* only tells us if the hardware supports hyperthreading.  If the feature */
/* is turned off in the BIOS, we don't detect this.  UPDATE: Detect some */
/* of these situations, by comparing number of cores to number of logical */
/* processors.  This test fails if the machine has multiple physical CPUs. */

/* Nfortino has suggested using the new x2apic code below.  It might correct */
/* some of the rare problems we were seeing on Core i7 machines. */

/* Determine if we should use leaf 11 topology enumeration. */
/* Otherwise, use leaf 1 + leaf 4 */

		if (max_cpuid_value >= 11) {
			reg.ECX = 0;
			Cpuid (11, &reg);
		}
		if (max_cpuid_value >= 11 && reg.EBX != 0) {
			unsigned int i, cores;
			CPU_HYPERTHREADS = reg.EBX & 0xFFFF;
			for (i = 1; i < 5; i++) {
				reg.ECX = i;
				Cpuid (11, &reg);
				if ((reg.EBX & 0xFFFF) == 0) break;
				cores = (reg.EBX & 0xFFFF) / CPU_HYPERTHREADS;
			}
			if (num_logical_processors <= cores) CPU_HYPERTHREADS = 1;
		} else if (max_cpuid_value >= 1) {
			Cpuid (1, &reg);
			if ((reg.EDX >> 28) & 0x1) {
				CPU_HYPERTHREADS = (reg.EBX >> 16) & 0xFF;
				if (CPU_HYPERTHREADS <= 1) CPU_HYPERTHREADS = 1;
				else if (max_cpuid_value >= 4) {
					unsigned int cores;
					reg.ECX = 0;
					Cpuid (4, &reg);
					cores = (reg.EAX >> 26) + 1;
					CPU_HYPERTHREADS /= cores;
					if (CPU_HYPERTHREADS < 2) CPU_HYPERTHREADS = 2;
					if (num_logical_processors <= cores) CPU_HYPERTHREADS = 1;
				}
				else CPU_HYPERTHREADS = 2;
			}
		}

/* Call CPUID with 2 argument.  It returns the cache size and structure */
/* in a series of 8-bit descriptors */

		if (max_cpuid_value >= 2) {
			Cpuid (2, &reg);
			if ((reg.EAX & 0xFF) > 0) {
				unsigned int descriptors[15];
				int i, count;
				count = 0;
				if (! (reg.EAX & 0x80000000)) {
					descriptors[count++] = (reg.EAX >> 24) & 0xFF;
					descriptors[count++] = (reg.EAX >> 16) & 0xFF;
					descriptors[count++] = (reg.EAX >> 8) & 0xFF;
				}
				if (! (reg.EBX & 0x80000000)) {
					descriptors[count++] = (reg.EBX >> 24) & 0xFF;
					descriptors[count++] = (reg.EBX >> 16) & 0xFF;
					descriptors[count++] = (reg.EBX >> 8) & 0xFF;
					descriptors[count++] = reg.EBX & 0xFF;
				}
				if (! (reg.ECX & 0x80000000)) {
					descriptors[count++] = (reg.ECX >> 24) & 0xFF;
					descriptors[count++] = (reg.ECX >> 16) & 0xFF;
					descriptors[count++] = (reg.ECX >> 8) & 0xFF;
					descriptors[count++] = reg.ECX & 0xFF;
				}
				if (! (reg.EDX & 0x80000000)) {
					descriptors[count++] = (reg.EDX >> 24) & 0xFF;
					descriptors[count++] = (reg.EDX >> 16) & 0xFF;
					descriptors[count++] = (reg.EDX >> 8) & 0xFF;
					descriptors[count++] = reg.EDX & 0xFF;
				}
				for (i = 0; i < count; i++) {
					switch (descriptors[i]) {
					case 0x03:
						CPU_L2_DATA_TLBS = 64;
						break;
					case 0x0A:
						CPU_L1_CACHE_SIZE = 8;
						CPU_L1_CACHE_LINE_SIZE = 32;
						CPU_L1_SET_ASSOCIATIVE = 2;
						break;
					case 0x0C:
						CPU_L1_CACHE_SIZE = 16;
						CPU_L1_CACHE_LINE_SIZE = 32;
						CPU_L1_SET_ASSOCIATIVE = 4;
						break;
					case 0x0D:
						CPU_L1_CACHE_SIZE = 16;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 4;
						break;
					case 0x0E:
						CPU_L1_CACHE_SIZE = 24;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 6;
						break;
					case 0x21:
						CPU_L2_CACHE_SIZE = 256;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x22:
						CPU_L3_CACHE_SIZE = 512;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 4;
						break;
					case 0x23:
						CPU_L3_CACHE_SIZE = 1024;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0x25:
						CPU_L3_CACHE_SIZE = 2048;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0x29:
						CPU_L3_CACHE_SIZE = 4096;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0x2C:
						CPU_L1_CACHE_SIZE = 32;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 8;
						break;
					case 0x39:
						CPU_L2_CACHE_SIZE = 128;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x3A:
						CPU_L2_CACHE_SIZE = 192;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 6;
						break;
					case 0x3B:
						CPU_L2_CACHE_SIZE = 128;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 2;
						break;
					case 0x3C:
						CPU_L2_CACHE_SIZE = 256;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x3D:
						CPU_L2_CACHE_SIZE = 384;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 6;
						break;
					case 0x3E:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 6;
						break;
					case 0x40:
						if (family == 15) {
							/* no L3 cache */
						} else {
							CPU_L2_CACHE_SIZE = 0;
						}
						break;
					case 0x41:
						CPU_L2_CACHE_SIZE = 128;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x42:
						CPU_L2_CACHE_SIZE = 256;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x43:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x44:
						CPU_L2_CACHE_SIZE = 1024;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x45:
						CPU_L2_CACHE_SIZE = 2048;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x46:
						CPU_L3_CACHE_SIZE = 4096;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 4;
						break;
					case 0x47:
						CPU_L3_CACHE_SIZE = 8192;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0x48:
						CPU_L2_CACHE_SIZE = 3072;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 12;
						break;
					case 0x49:
						if (family == 0x0F && model == 0x06) {
							CPU_L3_CACHE_SIZE = 4096;
							CPU_L3_CACHE_LINE_SIZE = 64;
							CPU_L3_SET_ASSOCIATIVE = 16;
						} else {
							CPU_L2_CACHE_SIZE = 4096;
							CPU_L2_CACHE_LINE_SIZE = 64;
							CPU_L2_SET_ASSOCIATIVE = 16;
						}
						break;
					case 0x4A:
						CPU_L3_CACHE_SIZE = 6144;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 12;
						break;
					case 0x4B:
						CPU_L3_CACHE_SIZE = 8192;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 16;
						break;
					case 0x4C:
						CPU_L3_CACHE_SIZE = 12288;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 12;
						break;
					case 0x4D:
						CPU_L3_CACHE_SIZE = 16384;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 16;
						break;
					case 0x4E:
						CPU_L2_CACHE_SIZE = 6144;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 24;
						break;
					case 0x5B:
					case 0xBA:
						CPU_L2_DATA_TLBS = 64;
						break;
					case 0x5C:
					case 0xB3:
						CPU_L2_DATA_TLBS = 128;
						break;
					case 0x5D:
					case 0xB4:
						CPU_L2_DATA_TLBS = 256;
						break;
					case 0x60:
						CPU_L1_CACHE_SIZE = 16;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 8;
						break;
					case 0x66:
						CPU_L1_CACHE_SIZE = 8;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 4;
						break;
					case 0x67:
						CPU_L1_CACHE_SIZE = 16;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 4;
						break;
					case 0x68:
						CPU_L1_CACHE_SIZE = 32;
						CPU_L1_CACHE_LINE_SIZE = 64;
						CPU_L1_SET_ASSOCIATIVE = 4;
						break;
					case 0x78:
						CPU_L2_CACHE_SIZE = 1024;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x79:
						CPU_L2_CACHE_SIZE = 128;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x7A:
						CPU_L2_CACHE_SIZE = 256;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x7B:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x7C:
						CPU_L2_CACHE_SIZE = 1024;
						CPU_L2_CACHE_LINE_SIZE = 128;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x7D:
						CPU_L2_CACHE_SIZE = 2048;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x7F:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 2;
						break;
					case 0x80:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x82:
						CPU_L2_CACHE_SIZE = 256;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x83:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x84:
						CPU_L2_CACHE_SIZE = 1024;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x85:
						CPU_L2_CACHE_SIZE = 2048;
						CPU_L2_CACHE_LINE_SIZE = 32;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0x86:
						CPU_L2_CACHE_SIZE = 512;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 4;
						break;
					case 0x87:
						CPU_L2_CACHE_SIZE = 1024;
						CPU_L2_CACHE_LINE_SIZE = 64;
						CPU_L2_SET_ASSOCIATIVE = 8;
						break;
					case 0xD0:
						CPU_L3_CACHE_SIZE = 512;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 4;
						break;
					case 0xD1:
						CPU_L3_CACHE_SIZE = 1024;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 4;
						break;
					case 0xD2:
						CPU_L3_CACHE_SIZE = 2048;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 4;
						break;
					case 0xD6:
						CPU_L3_CACHE_SIZE = 1024;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0xD7:
						CPU_L3_CACHE_SIZE = 2048;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0xD8:
						CPU_L3_CACHE_SIZE = 4096;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 8;
						break;
					case 0xDC:
						CPU_L3_CACHE_SIZE = 1536;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 12;
						break;
					case 0xDD:
						CPU_L3_CACHE_SIZE = 3072;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 12;
						break;
					case 0xDE:
						CPU_L3_CACHE_SIZE = 6144;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 12;
						break;
					case 0xE2:
						CPU_L3_CACHE_SIZE = 2048;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 16;
						break;
					case 0xE3:
						CPU_L3_CACHE_SIZE = 4096;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 16;
						break;
					case 0xE4:
						CPU_L3_CACHE_SIZE = 8192;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 16;
						break;
					case 0xEA:
						CPU_L3_CACHE_SIZE = 12288;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 24;
						break;
					case 0xEB:
						CPU_L3_CACHE_SIZE = 18432;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 24;
						break;
					case 0xEC:
						CPU_L3_CACHE_SIZE = 24576;
						CPU_L3_CACHE_LINE_SIZE = 64;
						CPU_L3_SET_ASSOCIATIVE = 24;
						break;
					}
				}
			}
		}

/* If we haven't figured out the L2 cache size, use 0x80000006 to deduce */
/* the cache size. */

		if (CPU_L2_CACHE_SIZE == -1 &&
		    max_extended_cpuid_value >= 0x80000006) {
			Cpuid (0x80000006, &reg);
			CPU_L2_CACHE_LINE_SIZE = reg.ECX & 0xFF;
			if ((reg.ECX >> 12 & 0xF) == 2)
				CPU_L2_SET_ASSOCIATIVE = 2;
			if ((reg.ECX >> 12 & 0xF) == 4)
				CPU_L2_SET_ASSOCIATIVE = 4;
			if ((reg.ECX >> 12 & 0xF) == 6)
				CPU_L2_SET_ASSOCIATIVE = 8;
			if ((reg.ECX >> 12 & 0xF) == 8)
				CPU_L2_SET_ASSOCIATIVE = 16;
			CPU_L2_CACHE_SIZE = (reg.ECX >> 16);
		}

/* If we still haven't figured out the L1, L2 or L3 cache size, use 0x00000004 to deduce */
/* the cache size. */

		if ((CPU_L1_CACHE_SIZE == -1 || CPU_L2_CACHE_SIZE == -1 || CPU_L3_CACHE_SIZE == -1) &&
		    max_cpuid_value >= 4) {
			int	i;
			for (i = 0; i < 10; i++) {
				int	cache_type, cache_level, cores, threads;
				int	associativity, partitions, line_size, sets;
				int	prefetch_stride;

				reg.ECX = i;
				Cpuid (4, &reg);
				cache_type = reg.EAX & 0x1F;
				if (cache_type == 0) break;
				if (cache_type == 2) continue;  /* Ignore instruction caches */
				cores = (reg.EAX >> 26) + 1;
				threads = ((reg.EAX >> 14) & 0xFFF) + 1;
				cache_level = (reg.EAX >> 5) & 0x7;
				associativity = (reg.EBX >> 22) + 1;
				partitions = ((reg.EBX >> 12) & 0x3FF) + 1;
				line_size = (reg.EBX & 0xFFF) + 1;
				sets = reg.ECX + 1;
				prefetch_stride = (reg.EDX & 0x3FF);
				if (prefetch_stride == 0) prefetch_stride = 64;
				/* Not documented, but 45nm core 2 returns prefetch_stride of 1 */
				/* Skylake-X returns an incorrect value for L2 cache size.  To compensate, in version 29.5 */
				/* we've made the code below supercede any previous cache size calculations. */
				if (prefetch_stride == 1) prefetch_stride = 64;
				if (cache_level == 1) {
					CPU_L1_CACHE_SIZE = (associativity * partitions * line_size * sets) >> 10;
					CPU_L1_SET_ASSOCIATIVE = associativity;
					CPU_L1_CACHE_LINE_SIZE = prefetch_stride;
				}
				if (cache_level == 2) {
					CPU_L2_CACHE_SIZE = (associativity * partitions * line_size * sets) >> 10;
					CPU_L2_SET_ASSOCIATIVE = associativity;
					CPU_L2_CACHE_LINE_SIZE = prefetch_stride;
				}
				if (cache_level == 3) {
					CPU_L3_CACHE_SIZE = (associativity * partitions * line_size * sets) >> 10;
					CPU_L3_SET_ASSOCIATIVE = associativity;
					CPU_L3_CACHE_LINE_SIZE = prefetch_stride;
				}
			}
		}

/* If we haven't figured out the brand string, create one based on the */
/* sample code in Intel's AP-485 document. */

		if (CPU_BRAND[0] == 0) {

			if (family == 4)
				strcpy (CPU_BRAND, "Intel 486 processor");

			else if (family == 5) {
				if (type == 0 && model <= 2)
					strcpy (CPU_BRAND, "Intel Pentium processor");
				else if (type == 1 && model <= 3)
					strcpy (CPU_BRAND, "Intel Pentium OverDrive processor");
				else if (type == 0 && model >= 4)
					strcpy (CPU_BRAND, "Intel Pentium MMX processor");
				else if (type == 1 && model >= 4)
					strcpy (CPU_BRAND, "Intel Pentium MMX OverDrive processor");
				else
					strcpy (CPU_BRAND, "Intel Pentium processor");
			}

			else if (family == 6 && model == 1)
				strcpy (CPU_BRAND, "Intel Pentium Pro processor");

			else if (family == 6 && model == 3) {
				if (type == 0)
					strcpy (CPU_BRAND, "Intel Pentium II processor");
				else
					strcpy (CPU_BRAND, "Intel Pentium II OverDrive processor");
			}

			else if (family == 6 && model == 5) {
				if (CPU_L2_CACHE_SIZE == 0)
					strcpy (CPU_BRAND, "Intel Celeron processor");
				else if (CPU_L2_CACHE_SIZE >= 1024)
					strcpy (CPU_BRAND, "Intel Pentium II Xeon processor");
				else
					strcpy (CPU_BRAND, "Intel Pentium II or Pentium II Xeon processor");
			}

			else if (family == 6 && model == 6)
				strcpy (CPU_BRAND, "Intel Celeron processor");

			else if (family == 6 && model == 7) {
				if (CPU_L2_CACHE_SIZE >= 1024)
					strcpy (CPU_BRAND, "Intel Pentium III Xeon processor");
				else
					strcpy (CPU_BRAND, "Intel Pentium III or Pentium III Xeon processor");
			}

			else if (family == 6 && model == 0xB &&
				 stepping_id == 1 && brand_index == 0x3)
				strcpy (CPU_BRAND, "Intel(R) Celeron(R) processor");

			else if (family == 15 && model == 1 &&
				 stepping_id == 3 && brand_index == 0xB)
				strcpy (CPU_BRAND, "Intel(R) Xeon(TM) processor MP");

			else if (family == 15 && model == 1 &&
				 stepping_id == 3 && brand_index == 0xE)
				strcpy (CPU_BRAND, "Intel(R) Xeon(TM) processor");

			else if (brand_index != 0 &&
				 brand_index < NUM_BRAND_NAMES)
				strcpy (CPU_BRAND, BRAND_NAMES[brand_index]);

/* If we've failed to figure out the brand string, create a default. */

			else if (CPU_BRAND[0] == 0)
				strcpy (CPU_BRAND, "Unknown Intel CPU");
		}
	}

/*---------------------------------------------------------------+
| Check for AMD vendor string.  Perform AMD-specific operations. |
+---------------------------------------------------------------*/

	else if (strcmp ((const char *) vendor_id, "AuthenticAMD") == 0) {
		unsigned long extended_feature_bits = 0;
		unsigned long advanced_power_mgmt = 0;

		if (max_extended_cpuid_value >= 0x80000001) {
			Cpuid (0x80000001, &reg);
			extended_feature_bits = reg.EDX;
		}

		if (max_extended_cpuid_value >= 0x80000007) {
			Cpuid (0x80000007, &reg);
			advanced_power_mgmt = reg.EDX;
		}

/* Deduce the cpu type given the family, model, stepping, etc.  If we */
/* haven't figured out the brand string, create one based on the cpu type. */

/* Information from AMD Processor Recognition Application Note, */
/* Publication # 20734 Revision: 3.07 February 2004 */
/* Chap.3 Table 4: Summary of Processor Signatures for AMD Processors */

		if (family_code == 4) {
			strcpy (CPU_BRAND, "AMD Am486 or Am5x86 processor");
		}
		if ((CPU_BRAND[0] == 0) || (strstr (CPU_BRAND, "Unknown"))) {
			if (family_code == 5) {
				if (model_number <= 3) {
					strcpy (CPU_BRAND, "AMD K5 processor");
				}
				else if ((model_number >= 6) && (model_number <= 7)) {
					strcpy (CPU_BRAND, "AMD K6 processor");
				}
				else if (model_number == 8) {
					strcpy (CPU_BRAND, "AMD K6-2 processor");
				}
				else if (model_number == 9) {
					strcpy (CPU_BRAND, "AMD K6-III processor");
				}
			}
			if (family_code == 6) {
				int mobileCPU = (advanced_power_mgmt & 7) == 7;
				int mp = 0 != (extended_feature_bits & (1 << 19));
				char *szCPU = "";

				switch (model_number)
				{
					case 1:
					case 2:
					case 4:
						szCPU = "AMD Athlon processor";
						break;
					case 3:
						szCPU = "AMD Duron processor";
						break;
					case 6:
						if (mobileCPU) {
							szCPU = "Mobile AMD Athlon 4 processor";
						}
						else {
							szCPU = (mp) ? "AMD Athlon MP processor" : "AMD Athlon XP processor";
						}
						break;
					case 7:
						szCPU = (mobileCPU) ? "Mobile AMD Duron processor" : "AMD Duron processor";
						break;
					case 8:
						szCPU = (mp) ? "AMD Athlon MP processor" : "AMD Athlon XP processor";
						break;
					case 10:
						if (mobileCPU) {
							szCPU = "Mobile AMD Athlon XP-M processor";
						}
						else {
							szCPU = (mp) ? "AMD Athlon MP processor" : "AMD Athlon XP processor";
						}
				}
				strcpy (CPU_BRAND, szCPU);
			}
			if (family_code == 15) {
				if (model_number == 4) {
					strcpy (CPU_BRAND, "AMD Athlon 64 processor");
				}
				else if (model_number == 5) {
					strcpy (CPU_BRAND, "AMD Opteron processor");
				}
			}
		}

/* Early Athlon CPUs support the SSE prefetch instructions even though */
/* they do not support the full SSE instruction set.  I think testing for */
/* the AMD MMX extensions capability will detect this case. */

		if (max_extended_cpuid_value >= 0x80000001 && ! (CPU_FLAGS & CPU_PREFETCH)) {
			Cpuid (0x80000001, &reg);
			if ((reg.EDX >> 22) & 0x1 && canExecInstruction (CPU_PREFETCH))
				CPU_FLAGS |= CPU_PREFETCH;
		}

/* Check for support of 3DNow! instructions.  The prefetchw instruction */
/* from the 3DNow! instruction set is used by the assembly code. */
/* Starting with Bulldozer, AMD stopped supporting 3DNow! but kept */
/* support for the 3DNow! prefetch instructions.  */

		if (max_extended_cpuid_value >= 0x80000001) {
			Cpuid (0x80000001, &reg);
			if ((reg.EDX >> 31) & 0x1 && canExecInstruction (CPU_3DNOW))
				CPU_FLAGS |= CPU_3DNOW + CPU_3DNOW_PREFETCH;
			else if ((reg.ECX >> 8) & 0x1 && canExecInstruction (CPU_3DNOW))
				CPU_FLAGS |= CPU_3DNOW_PREFETCH;
		}

/* Get the L1 cache size and number of data TLBs */

		if (max_extended_cpuid_value >= 0x80000005) {
			Cpuid (0x80000005, &reg);
			CPU_L1_DATA_TLBS = (reg.EBX >> 16) & 0xFF;
			CPU_L1_CACHE_SIZE = (reg.ECX >> 24) & 0xFF;
			CPU_L1_CACHE_LINE_SIZE = reg.ECX & 0xFF;
		}

/* Get the L2 and L3 cache sizes */

		if (max_extended_cpuid_value >= 0x80000006) {
			Cpuid (0x80000006, &reg);
			CPU_L2_DATA_TLBS = (reg.EBX >> 16) & 0xFFF;
			CPU_L2_CACHE_SIZE = (reg.ECX >> 16) & 0xFFFF;
			if (CPU_L2_CACHE_SIZE == 1) /* Workaround Duron bug */
				CPU_L2_CACHE_SIZE = 64;
			CPU_L2_CACHE_LINE_SIZE = reg.ECX & 0xFF;
			CPU_L2_SET_ASSOCIATIVE = (reg.ECX >> 8) & 0xF;
			if (CPU_L2_SET_ASSOCIATIVE == 0x2)
				CPU_L2_SET_ASSOCIATIVE = 2;
			else if (CPU_L2_SET_ASSOCIATIVE == 0x4)
				CPU_L2_SET_ASSOCIATIVE = 4;
			else if (CPU_L2_SET_ASSOCIATIVE == 0x6)
				CPU_L2_SET_ASSOCIATIVE = 8;
			else if (CPU_L2_SET_ASSOCIATIVE == 0x8)
				CPU_L2_SET_ASSOCIATIVE = 16;
			else if (CPU_L2_SET_ASSOCIATIVE == 0xA)
				CPU_L2_SET_ASSOCIATIVE = 32;
			else if (CPU_L2_SET_ASSOCIATIVE == 0xB)
				CPU_L2_SET_ASSOCIATIVE = 48;
			else if (CPU_L2_SET_ASSOCIATIVE == 0xC)
				CPU_L2_SET_ASSOCIATIVE = 64;
			else if (CPU_L2_SET_ASSOCIATIVE == 0xD)
				CPU_L2_SET_ASSOCIATIVE = 96;
			else if (CPU_L2_SET_ASSOCIATIVE == 0xE)
				CPU_L2_SET_ASSOCIATIVE = 128;

			CPU_L3_DATA_TLBS = -1;
			CPU_L3_CACHE_SIZE = (reg.EDX >> 18) * 512;
			CPU_L3_CACHE_LINE_SIZE = reg.EDX & 0xFF;
			CPU_L3_SET_ASSOCIATIVE = (reg.EDX >> 8) & 0xF;
			if (CPU_L3_SET_ASSOCIATIVE == 0x2)
				CPU_L3_SET_ASSOCIATIVE = 2;
			else if (CPU_L3_SET_ASSOCIATIVE == 0x4)
				CPU_L3_SET_ASSOCIATIVE = 4;
			else if (CPU_L3_SET_ASSOCIATIVE == 0x6)
				CPU_L3_SET_ASSOCIATIVE = 8;
			else if (CPU_L3_SET_ASSOCIATIVE == 0x8)
				CPU_L3_SET_ASSOCIATIVE = 16;
			else if (CPU_L3_SET_ASSOCIATIVE == 0xA)
				CPU_L3_SET_ASSOCIATIVE = 32;
			else if (CPU_L3_SET_ASSOCIATIVE == 0xB)
				CPU_L3_SET_ASSOCIATIVE = 48;
			else if (CPU_L3_SET_ASSOCIATIVE == 0xC)
				CPU_L3_SET_ASSOCIATIVE = 64;
			else if (CPU_L3_SET_ASSOCIATIVE == 0xD)
				CPU_L3_SET_ASSOCIATIVE = 96;
			else if (CPU_L3_SET_ASSOCIATIVE == 0xE)
				CPU_L3_SET_ASSOCIATIVE = 128;
		}

/* Try to determine the CPU architecture */

		Cpuid (0x80000001, &reg);
		if ((reg.ECX >> 16) & 0x1)
			CPU_FLAGS |= CPU_FMA4;

		if (! (CPU_FLAGS & CPU_SSE2))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PRE_SSE2;
		else if (family_code == 15 && (extended_family == 8 || extended_family == 10))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_ZEN;
		else if (family_code == 15 && extended_family >= 9)		// Future AMD processors
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_OTHER;
// Do we need to check for Bobcat and Jaguar family codes here?
// The code below will return K10 for Bobcat and Bulldozer for Jaguar
// See https://en.wikipedia.org/wiki/List_of_AMD_CPU_microarchitectures
		else if (CPU_FLAGS & CPU_AVX)
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_BULLDOZER;
		else if (max_extended_cpuid_value < 0x8000001A)
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_K8;
		else {
			Cpuid (0x8000001A, &reg);
			if (! (reg.EAX & 1))
				CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_K8;
			else
				CPU_ARCHITECTURE = CPU_ARCHITECTURE_AMD_K10;
		}

/* If we haven't figured out the brand string, create a default one */

		if (CPU_BRAND[0] == 0)
			strcpy (CPU_BRAND, "Unknown AMD CPU");
	}

/*---------------------------------------------------------------+
| Check for VIA vendor string.  Perform VIA-specific operations. |
+---------------------------------------------------------------*/

	else if (strcmp ((const char *) vendor_id, "CentaurHauls") == 0) {

/* Get the L1 cache size and number of data TLBs */

		if (max_extended_cpuid_value >= 0x80000005) {
			Cpuid (0x80000005, &reg);
			CPU_L2_DATA_TLBS = (reg.EBX >> 16) & 0xFF;
			CPU_L1_CACHE_SIZE = (reg.ECX >> 24) & 0xFF;
			CPU_L1_CACHE_LINE_SIZE = reg.ECX & 0xFF;
		}

/* Get the L2 cache size */

		if (max_extended_cpuid_value >= 0x80000006) {
			Cpuid (0x80000006, &reg);
			if (family_code < 6 || (family_code == 6 && model_number <= 7))		// Older VIA processors
				CPU_L2_CACHE_SIZE = (reg.ECX >> 24) & 0xFF;
			else									// Newer VIA processors
				CPU_L2_CACHE_SIZE = (reg.ECX >> 16) & 0xFFFF;
			CPU_L2_CACHE_LINE_SIZE = reg.ECX & 0xFF;
		}

/* Try to determine the CPU architecture */

		if (! (CPU_FLAGS & CPU_SSE2))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PRE_SSE2;
		else
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_OTHER;

/* If we haven't figured out the brand string, create a default one */

		if (CPU_BRAND[0] == 0)
			strcpy (CPU_BRAND, "Unknown VIA/CYRIX CPU");
	}

/*--------------------------------------------------------+
| An unknown CPU vendor.  Fill in defaults as best we can |
+--------------------------------------------------------*/

	else {
		if (! (CPU_FLAGS & CPU_SSE2))
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_PRE_SSE2;
		else
			CPU_ARCHITECTURE = CPU_ARCHITECTURE_OTHER;

		if (CPU_BRAND[0] == 0) {
			strcpy (CPU_BRAND, "Unrecognized CPU vendor: ");
			strcat (CPU_BRAND, vendor_id);
		}
	}

/* Now calculate the CPU_CORES value -- the number of physical */
/* processors/cores. */

	CPU_CORES = num_logical_processors / CPU_HYPERTHREADS;
	if (CPU_CORES < 1) CPU_CORES = 1;
}

void guessCpuSpeed (void)
{

/* If RDTSC is not supported, then measuring the CPU speed is real hard */
/* so we just assume the CPU speed is 100 MHz.  This isn't a big deal */
/* since CPUs not supporting RDTSC aren't powerful enough to run prime95. */

	if (! (CPU_FLAGS & CPU_RDTSC)) {
		CPU_SPEED = 100.0;
		return;
	}

/* If this machine supports a high resolution counter, use that for timing */

	if (isHighResTimerAvailable ()) {
		uint32_t start_hi, start_lo, end_hi, end_lo;
		double	frequency, rdtsc_multiplier, temp, start_time, end_time;
		unsigned long iterations, min_100000, min_1000000;
		double	speed1, speed2, speed3, avg_speed;
		int	tries;

/* In Sandy Bridge and some or all of the Core 2 series CPUs, RDTSC no longer */
/* counts clock ticks.  Intel did this so that RDTSC can make accurate timing */
/* as SpeedStep changes the CPU frequency.  For such CPUs we try to find a */
/* multiplier that will convert RDTSC clocks into CPU core clocks.  Note that */
/* this isn't a perfect solution because some CPUs have a wide range of CPU core */
/* speeds (such as the i7-Q840M which ranges from 1.87 to 3.2 GHz).  We try to */
/* get the non-turbo-boost core speed by loading up all but one core before */
/* doing our timings. */

		rdtsc_multiplier = 1.0;
		if (CPU_FLAGS & CPU_TSC_INVARIANT) {
			unsigned int i;
			gwthread thread_id;
			// Start threads so that all cores are doing work.  This will
			// hopefully let us measure the CPU core speed when all cores
			// are busy rather than the speed when cores are Turbo boosted.
			end_busy_loop = FALSE;
			for (i = 1; i <= CPU_CORES-1; i++) gwthread_create (&thread_id, &busy_loop, NULL);
			// Run several million clocks in hopes of ramping up the core's
			// clock speed to the maximum allowed by SpeedStep.
			for (tries = 1; tries <= 50; tries++) one_million_clocks ();
			// Find how many RDTSC clock ticks occur in 900,000 CPU clock ticks.
			for (tries = 1; tries <= 10; tries++) {
				rdtsc (&start_hi, &start_lo);
				one_hundred_thousand_clocks ();
				rdtsc (&end_hi, &end_lo);
				if (tries == 1 || min_100000 > (end_lo - start_lo)) min_100000 = (end_lo - start_lo);
				rdtsc (&start_hi, &start_lo);
				one_million_clocks ();
				rdtsc (&end_hi, &end_lo);
				if (tries == 1 || min_1000000 > (end_lo - start_lo)) min_1000000 = (end_lo - start_lo);
			}
			rdtsc_multiplier = 900000.0 / (double) (min_1000000 - min_100000);
			// On hyperthreaded machines (like my i7-860) the routines for
			// 100,000 and 1,000,000 clocks will not work accurately when
			// hyperthreaded cores are busy.  As a sanity check, make sure
			// rdtsc_multiplier does not fall below 1.0.
			if (CPU_HYPERTHREADS > 1 && rdtsc_multiplier < 1.0) rdtsc_multiplier = 1.0;
			// Terminate the busy loop threads
			end_busy_loop = TRUE;
		}

/* Compute the number of high resolution ticks in one millisecond */
/* This should give us good accuracy while hopefully avoiding time slices */

		frequency = getHighResTimerFrequency ();
		iterations = (unsigned long) (frequency / 1000.0);

/* Do up to 20 iterations (idea lifted from Intel's code) until 3 straight */
/* speed calculations are within 1 MHz of each other.  This is good since */
/* outside forces can interfere with this calculation. */

		tries = 0; speed1 = 0.0; speed2 = 0.0;
		do {

/* Shuffle the last calculations, bump counter */

			speed3 = speed2;
			speed2 = speed1;
			tries++;

/* Loop waiting for high resolution timer to change */

			temp = getHighResTimer ();
			while ((start_time = getHighResTimer ()) == temp);
			rdtsc (&start_hi, &start_lo);

/* Now loop waiting for timer to tick off about 1 millisecond */

			temp = start_time + (double) iterations;
			while ((end_time = getHighResTimer ()) < temp);
			rdtsc (&end_hi, &end_lo);

/* Compute speed based on number of clocks in the time interval */

			speed1 = (end_hi * 4294967296.0 + end_lo -
				  start_hi * 4294967296.0 - start_lo) * rdtsc_multiplier *
				 frequency /
				 (end_time - start_time) / 1000000.0;

/* Calculate average of last 3 speeds.  Loop if this average isn't */
/* very close to all of the last three speed calculations. */

			avg_speed = (speed1 + speed2 + speed3) / 3.0;
		} while (tries < 3 ||
		         (tries < 20 &&
		          (fabs (speed1 - avg_speed) > 1.0 ||
		           fabs (speed2 - avg_speed) > 1.0 ||
		           fabs (speed3 - avg_speed) > 1.0)));

/* Final result is average speed of last three calculations */

		CPU_SPEED = avg_speed;
	}

/* Otherwise use the low resolution timer to measure CPU speed */

	else {
		struct timeval temp, start_time, end_time;
		uint32_t start_hi, start_lo, end_hi, end_lo;
		double	speed1, speed2, speed3, avg_speed, elapsed_time;
		int	tries;

/* Do up to 10 iterations until 3 straight speed calculations are within */
/* 1 MHz of each other. */

		tries = 0; speed1 = 0.0; speed2 = 0.0;
		do {

/* Shuffle the last calculations, bump counter */

			speed3 = speed2;
			speed2 = speed1;
			tries++;

/* Loop waiting for low resolution timer to change */

			gettimeofday (&temp, NULL);
			do
				gettimeofday (&start_time, NULL);
			while (temp.tv_usec == start_time.tv_usec);
			rdtsc (&start_hi, &start_lo);

/* Now loop waiting for timer to change again */

			do
				gettimeofday (&end_time, NULL);
			while (start_time.tv_usec == end_time.tv_usec);
			rdtsc (&end_hi, &end_lo);

/* Compute elapsed time.  Since most PCs have a low resolution clock */
/* that ticks every 18.20648193 seconds, then if elapsed time is close */
/* to 1 / 18.2 seconds, then assume the elapsed time is */
/* 1 / 18.206... = 0.054925493 seconds. */

			elapsed_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
			if (elapsed_time >= 0.049 && elapsed_time <= 0.061)
				elapsed_time = 0.054925493;

/* Compute speed based on number of clocks in the time interval */

			speed1 = (end_hi * 4294967296.0 + end_lo - start_hi * 4294967296.0 - start_lo) / elapsed_time / 1000000.0;

/* Caclulate average of last 3 speeds.  Loop if this average isn't */
/* very close to all of the last three speed calculations. */

			avg_speed = (speed1 + speed2 + speed3) / 3.0;
		} while (tries < 3 ||
		         (tries < 10 &&
		          (fabs (speed1 - avg_speed) > 1.0 ||
		           fabs (speed2 - avg_speed) > 1.0 ||
		           fabs (speed3 - avg_speed) > 1.0)));

/* Final result is average speed of last three calculations */

		CPU_SPEED = avg_speed;
	}
}

/*--------------------------------------------------------------------------
| And now, the routines that access the high resolution performance counter.
+-------------------------------------------------------------------------*/

int isHighResTimerAvailable (void)
{
#ifdef _WIN32
	LARGE_INTEGER large;
	return (QueryPerformanceCounter (&large));
#endif
#ifdef __OS2__
/* DosTmrQueryTime/DosTmrQueryFreq functions use the 8253/4 timer chip to */
/* obtain a timestamp.  DosTmrQueryTime returns the current count, and */
/* DosTmrQueryFreq returns the frequency at which the counter increments. */
/* This frequency is about 1.1MHz, which gives you a timer that's accurate */
/* to the microsecond. */
        return (TRUE);
#endif
#if defined (__linux__) || defined (__FreeBSD__) || defined (__APPLE__) || defined (__HAIKU__)
	struct timeval start, end;
	int	i;

/* Return true if gettimeofday is more accurate than 1/10 millisecond. */
/* Try 10 times to see if gettimeofday returns two values less than */
/* 100 microseconds apart. */

	for (i = 0; i < 10; i++) {
		gettimeofday (&start, NULL);
		for ( ; ; ) {
			gettimeofday (&end, NULL);
			if (start.tv_sec != end.tv_sec) break;
			if (start.tv_usec == end.tv_usec) continue;
			if (end.tv_usec - start.tv_usec < 100) return (TRUE);
		}
	}
	return (FALSE);
#endif
}

double getHighResTimer (void)
{
#ifdef _WIN32
	LARGE_INTEGER large;

	QueryPerformanceCounter (&large);
	return ((double) large.HighPart * 4294967296.0 +
		(double) /*(unsigned long)*/ large.LowPart);
#endif
#ifdef __OS2__
        unsigned long long qwTmrTime;
        DosTmrQueryTime((PQWORD)&qwTmrTime);
        return (qwTmrTime);
#endif
#if defined (__linux__) || defined (__FreeBSD__) || defined (__APPLE__) || defined (__HAIKU__)
	struct timeval x;
	gettimeofday (&x, NULL);
	return ((double) x.tv_sec * 1000000.0 + (double) x.tv_usec);
#endif
}

double getHighResTimerFrequency (void)
{
#ifdef _WIN32
	LARGE_INTEGER large;

	QueryPerformanceFrequency (&large);
	return ((double) large.HighPart * 4294967296.0 +
		(double) /*(unsigned long)*/ large.LowPart);
#endif
#ifdef __OS2__
	ULONG ulTmrFreq;
	DosTmrQueryFreq(&ulTmrFreq);
	return (ulTmrFreq);
#endif
#if defined (__linux__) || defined (__FreeBSD__) || defined (__APPLE__) || defined (__HAIKU__)
	return (1000000.0);
#endif
}

