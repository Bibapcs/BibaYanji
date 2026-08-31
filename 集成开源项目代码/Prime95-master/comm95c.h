/* Copyright 1995-2021 Mersenne Research, Inc. */
/* Author:  George Woltman */
/* Email: woltman@alum.mit.edu */

/* Handle differences between Windows and Linux runtime libraries */

#define __read(a,b,c)	_read(a,b,(unsigned int)(c))			// Linux 3rd arg is size_t, Windows is unsigned int
#define __write(a,b,c)	_write(a,b,(unsigned int)(c))			// Linux 3rd arg is size_t, Windows is unsigned int

/* Common definitions in Prime95 and NTPrime */

void getWindowsSerialNumber (char *);
void getWindowsSID (char *);
void getWindowsSerialNumber_2 (char *);
void getWindowsSID_2 (char *);

