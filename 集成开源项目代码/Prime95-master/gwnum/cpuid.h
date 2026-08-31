/*----------------------------------------------------------------------
| Copyright 1995-2017 Mersenne Research, Inc.  All rights reserved
| Author:  George Woltman
| Email: woltman@alum.mit.edu
|
| This file contains routines to determine the CPU type and speed.
| Plus, as a bonus, you get 3 routines to portably access the high
| resolution timer.
+---------------------------------------------------------------------*/

#ifndef _CPUID_H
#define _CPUID_H

/* This is a C library.  If used in a C++ program, don't let the C++ */
/* compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/* Include common definitions */

#include "gwcommon.h"

/* Routines that the user should call.  When calling */
/* guessCpuSpeed, affinity should be set to run on any CPU core */

void guessCpuType (void);
void guessCpuSpeed (void);

/* Routines to access the high resolution timer */

int isHighResTimerAvailable (void);
double getHighResTimer (void);
double getHighResTimerFrequency (void);

/* Global variables describing the CPU we are running on */

extern char CPU_BRAND[49];		/* Text description of CPU */
extern double CPU_SPEED;		/* Actual CPU Speed in MHz */
#define CPU_RDTSC		0x0001	/* Read timestamp counter supported */
#define CPU_TSC_INVARIANT	0x0002	/* RDTSC does NOT count actual CPU clocks! */
#define CPU_CMOV		0x0004	/* CMOV instruction supported */
#define CPU_PREFETCH		0x0008	/* SSE Prefetch instructions supported */
#define CPU_TLB_PRIMING		0x0010	/* Prefetching requires TLB priming.  Early Pentium 4's require priming. */
#define CPU_MMX			0x0020	/* MMX instructions supported */
#define CPU_3DNOW		0x0040	/* 3DNow! instructions supported */
#define CPU_3DNOW_PREFETCH	0x0080	/* 3DNow! prefetch and prefetchw instructions supported */
#define CPU_SSE			0x0100	/* SSE instructions supported */
#define CPU_SSE2		0x0200	/* SSE2 instructions supported */
#define CPU_SSE3		0x0400	/* SSE3 instructions supported */
#define CPU_SSSE3		0x0800	/* Supplemental SSE3 instructions supported */
#define CPU_SSE41		0x1000	/* SSE4.1 instructions supported */
#define CPU_SSE42		0x2000	/* SSE4.2 instructions supported */
#define CPU_AVX			0x4000	/* AVX instructions supported */
#define CPU_FMA3		0x8000	/* Intel fused multiply-add instructions supported */
#define CPU_FMA4		0x10000	/* AMD fused multiply-add instructions supported */
#define CPU_AVX2		0x20000	/* AVX2 instructions supported */
#define CPU_PREFETCHW		0x40000	/* PREFETCHW (the Intel version) instruction supported */
#define CPU_PREFETCHWT1		0x80000	/* PREFETCHWT1 instruction supported */
#define CPU_AVX512F		0x100000/* AVX512F instructions supported */
#define CPU_AVX512PF		0x200000/* AVX512PF instructions supported */
extern unsigned int CPU_FLAGS;		/* Cpu capabilities */
extern unsigned int CPU_CORES;		/* Number CPU cores */
extern unsigned int CPU_HYPERTHREADS;	/* Number of virtual processors that each CPU core supports. */
					/* Total number logical processors is CPU_CORES * CPU_HYPERTHREADS */
extern int CPU_L1_CACHE_SIZE;		/* In KB */
extern int CPU_L2_CACHE_SIZE;		/* In KB */
extern int CPU_L3_CACHE_SIZE;		/* In KB */
extern int CPU_L1_CACHE_LINE_SIZE;
extern int CPU_L2_CACHE_LINE_SIZE;
extern int CPU_L3_CACHE_LINE_SIZE;
extern int CPU_L1_DATA_TLBS;
extern int CPU_L2_DATA_TLBS;
extern int CPU_L3_DATA_TLBS;
extern int CPU_L1_SET_ASSOCIATIVE;
extern int CPU_L2_SET_ASSOCIATIVE;
extern int CPU_L3_SET_ASSOCIATIVE;

extern unsigned int CPU_SIGNATURE;	/* Vendor-specific family number, model number, stepping ID, etc. */

#define CPU_ARCHITECTURE_PRE_SSE2	0
#define CPU_ARCHITECTURE_PENTIUM_4	1
#define CPU_ARCHITECTURE_PENTIUM_M	2
#define CPU_ARCHITECTURE_CORE		3		/* Core Solo and Core Duo */
#define CPU_ARCHITECTURE_CORE_2		4
#define CPU_ARCHITECTURE_CORE_I7	5		/* Core i3/i5/i7 */
#define CPU_ARCHITECTURE_ATOM		6
#define CPU_ARCHITECTURE_PHI		7		/* Xeon Phi, Knights Landing with AVX-512 */
#define CPU_ARCHITECTURE_INTEL_OTHER	99
#define CPU_ARCHITECTURE_AMD_K8		100
#define CPU_ARCHITECTURE_AMD_K10	101
#define CPU_ARCHITECTURE_AMD_BULLDOZER	102
#define CPU_ARCHITECTURE_AMD_ZEN	103
#define CPU_ARCHITECTURE_AMD_OTHER	199
#define CPU_ARCHITECTURE_OTHER		999
extern int CPU_ARCHITECTURE;		/* Our attempt to derive the CPU architecture. */

/* Assembly language structures and routines */

struct cpuid_data {
	uint32_t EAX;		/* For communicating with asm routines */
	uint32_t EBX;
	uint32_t ECX;
	uint32_t EDX;
};

unsigned long ecpuidsupport (void);
void ecpuid (struct cpuid_data *);

/* Cleaner access to cpuid assembly code */

#define isCpuidSupported()	ecpuidsupport ()
#define Cpuid(a,s)		{ (s)->EAX=a; ecpuid (s); }

/* Cleaner access to xgetbv assembly code */

void exgetbv (struct cpuid_data *);
#define Xgetbv(a,s)		{ (s)->ECX=a; exgetbv (s); }

/* Routine used to time code chunks */
void erdtsc (uint32_t *hi, uint32_t *lo);
#define rdtsc(hi,lo)		erdtsc(hi,lo)

/* Init the x87 FPU, probably not needed any longer */
void fpu_init (void);

/* Masm routines to burn up a specific number of clocks */

void one_hundred_thousand_clocks ();
void one_million_clocks ();

#ifdef __cplusplus
}
#endif

#endif
