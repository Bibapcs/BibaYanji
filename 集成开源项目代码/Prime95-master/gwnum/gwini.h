/*----------------------------------------------------------------------
| gwini.h
|
| This file contains the headers and definitions that are used to implement
| reading and writing "INI" files.  These routines were developed over many years
| for prime95, so there are several routines that are not used by gwnum.
|
| NOTE:  These routines only work if you open no more than 10 ini files.  Also,
| you must not change the working directory at any time during program execution.
|
| ALSO NOTE:  Sorry, memory allocation errors are not handled properly and
| documentation is incredibly poor.
|
|  Copyright 2016-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWINI_H
#define _GWINI_H

/* This is a C library.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
*                             INI file Routines                               *
******************************************************************************/

void IniGetString (const char *, const char *, char *, unsigned int, const char *);
void IniSectionGetString (const char *, const char *, const char *, char *, unsigned int, const char *);
void IniGetNthString (const char *, const char *, int, char *, unsigned int, const char *);
void IniSectionGetNthString (const char *, const char *, const char *, int, char *, unsigned int, const char *);

void IniGetTimedString (const char *, const char *, char *, unsigned int, const char *, unsigned int *);
void IniSectionGetTimedString (const char *, const char *, const char *, char *, unsigned int, const char *, unsigned int *);
void IniSectionGetNthTimedString (const char *, const char *, const char *, int, char *, unsigned int, const char *, unsigned int *);

long IniGetInt (const char *, const char *, long);
long IniSectionGetInt (const char *, const char *, const char *, long);
long IniGetTimedInt (const char *, const char *, long, unsigned int *);
long IniSectionGetTimedInt (const char *, const char *, const char *, long, unsigned int *);

int64_t IniGetInt64 (const char *, const char *, int64_t);
int64_t IniSectionGetInt64 (const char *, const char *, const char *, int64_t);
int64_t IniGetTimedInt64 (const char *, const char *, int64_t, unsigned int *);
int64_t IniSectionGetTimedInt64 (const char *, const char *, const char *, int64_t, unsigned int *);

float IniGetFloat (const char *, const char *, float);
float IniSectionGetFloat (const char *, const char *, const char *, float);
float IniGetTimedFloat (const char *, const char *, float, unsigned int *);
float IniSectionGetTimedFloat (const char *, const char *, const char *, float, unsigned int *);

void IniWriteString (const char *, const char *, const char *);
void IniSectionWriteString (const char *, const char *, const char *, const char *);
void IniWriteNthString (const char *, const char *, int, const char *);
void IniSectionWriteNthString (const char *, const char *, const char *, int, const char *);

void IniWriteInt (const char *, const char *, long);
void IniSectionWriteInt (const char *, const char *, const char *, long);

void IniWriteInt64 (const char *, const char *, int64_t);
void IniSectionWriteInt64 (const char *, const char *, const char *, int64_t);

void IniWriteFloat (const char *, const char *, float);
void IniSectionWriteFloat (const char *, const char *, const char *, float);

/* More obscure INI file routines */

void IniDelayWrites (const char *);
void IniResumeImmediateWrites (const char *);

extern void (*INI_ERROR_CALLBACK)(const char *, int, const char *);	/* Callback routine when illegal line read from INI file. */
									/* Arguments are file name, line number, text on the line. */
#define IniSetErrorCallback(n)	(INI_ERROR_CALLBACK = n)

void IniFileReread (const char *);					/* Force the INI file to be re-read from disk */
void IniAddFileMerge (const char *, const char *, const char *);	/* Merge one INI file into another.  Prime95 calls these .add files */

const char *IniSectionGetStringRaw (const char *, const char *, const char *);
const char *IniSectionGetNthStringRaw (const char *, const char *, const char *, int);

#ifdef __cplusplus
}
#endif

#endif
