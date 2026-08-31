/*----------------------------------------------------------------------
| Copyright 1995-2021 Mersenne Research, Inc.  All rights reserved
+---------------------------------------------------------------------*/

#ifndef _ECM_H
#define _ECM_H

/* This is used by C and C++ code.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

int ecm (int, struct PriorityInfo *, struct work_unit *);
int pminus1 (int, struct PriorityInfo *, struct work_unit *);
int pplus1 (int, struct PriorityInfo *, struct work_unit *);
int pfactor (int, struct PriorityInfo *, struct work_unit *);
double guess_pminus1_probability (struct work_unit *w);

int setN (int, struct work_unit *, giant *);
int ecm_QA (int, struct PriorityInfo *);
int pminus1_QA (int, struct PriorityInfo *);
int pplus1_QA (int, struct PriorityInfo *);

#ifdef __cplusplus
}
#endif

#endif
