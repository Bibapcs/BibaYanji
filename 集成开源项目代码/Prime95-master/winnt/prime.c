/* Copyright 1995-2023 Mersenne Research, Inc. */
/* Author:  George Woltman */
/* Email: woltman@alum.mit.edu */

#include "windows.h"
#include <winnls.h>
#include <tchar.h>
#include "main.h"
#include "prime95.h"
#include <direct.h>
#include <ctype.h>
#include <dos.h>
#include <sys/timeb.h>

#include "common.h"
#include "pm1prob.h"
#include "md5.c"
#include "commona.c"
#include "commonb.c"
#include "commonc.c"
#include "comm95b.c"
//#define _WIN32_WINNT 0x0502	// (Windows 2003 Server - same as afx_v32.h for sddl.h)
#include "comm95c.c"
#include "primenet.c"
#include "proof_upload.c"
#include "proof_getdata.c"
#include "gwtest.c"

void create_window (
	int	thread_num)
{
}

void destroy_window (
	int	thread_num)
{
}

void title (
	int	thread_num,
	const char *msg)
{
}

void base_title (
	int	thread_num,
	const char *msg)
{
}

void flashWindowAndBeep ()
{
	MessageBeep (0xFFFFFFFF);
}

void RealOutputStr (
	int	thread_num,
	const char *buf)
{
	if (DEBUGGING) printf ("thread_num %d: %s", thread_num, buf);
}

void ChangeIcon (
	int	thread_num,
	int	icon_id)
{
}

void BlinkIcon (
	int	thread_num,
	int	duration)
{
}

void stopCheckCallback (
	int	thread_num)
{
}

/* Do some work prior to launching worker threads */

void PreLaunchCallback (
	int	launch_type)
{

// Stall if we've just booted (within 5 minutes of Windows starting)

	if (GetTickCount () < 300000 && launch_type == LD_CONTINUE) {
		int	delay;
		delay = IniGetInt (INI_FILE, "BootDelay", 90);
		delay -= GetTickCount () / 1000;
		if (delay > 0) Sleep (delay * 1000);
	}
}

/* Do some work after worker threads have terminated */

void PostLaunchCallback (
			 int	launch_type)
{
}

