/*----------------------------------------------------------------------
| gwcommon.h
|
| This file contains handy #defines that I use in all my projects
| 
|  Copyright 2005-2021 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWCOMMON_H
#define _GWCOMMON_H

#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

/* In many cases where the C code is interfacing with the assembly code */
/* we must declare variables that are exactly 32 or 64 bits wide.  This is the */
/* portable way to do this.  Finally supported in Visual Studio 2013. */

#include "inttypes.h"
#ifndef __cplusplus
#include "stdbool.h"
#endif

/* Define the ASSERT macro I use while debugging */

#include <assert.h>
#ifdef GDEBUG
#define ASSERTG		assert
#else
#define ASSERTG(a)
#endif
#define GWASSERT  	assert

#endif
