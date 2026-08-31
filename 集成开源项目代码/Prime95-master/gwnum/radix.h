/*----------------------------------------------------------------------
| radix.h
|
| This file contains the C routine definitions for radix conversion
| when required by gianttogw or gwtogiant.
| 
|  Copyright 2020 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

int nonbase2_gianttogw (	/* Returns an error code or zero for success */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	giant	g,		/* Input giant */
	gwnum	x);		/* Output gwnum */

int nonbase2_gwtogiant (	/* Returns an error code or zero for success */
	gwhandle *gwdata,	/* Handle initialized by gwsetup */
	gwnum	x,		/* Input gwnum */
	giant	g);		/* Output giant */


