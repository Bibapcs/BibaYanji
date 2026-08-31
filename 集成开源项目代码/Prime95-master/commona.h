/* Copyright 1995-2020 Mersenne Research, Inc.  All rights reserved */

/* Common strings */

#define MANUAL_QUIT	"You have elected to remove this computer from the Great Internet Mersenne Prime Search.  Other computers using this user ID will not be affected.  Please send the file results.txt to woltman@alum.mit.edu.\n\nAre you sure you want to do this?"
#define PRIMENET_QUIT	"You have elected to remove this computer from the Great Internet Mersenne Prime Search.  Other computers using this user ID will not be affected.\n\nPlease make sure your results have been successfully sent to the server (the program will be idle rather than looping trying to contact the server) before uninstalling the program. If in doubt, you can send the results.txt file to woltman@alum.mit.edu.\n\nYou can either complete your current assignment or you can quit GIMPS immediately.  Do you wish to complete your current work assignments before quitting?"
#define PING_ERROR	"Unable to get version information from PrimeNet server."
#define MSG_MEMORY	"You have left the available memory fields at 8 megabytes.  You can increase your chances of finding a Mersenne prime very slightly if you let the program occasionally use more memory.  The readme.txt file has more information.  Do you want to let the program use more memory?"
#define MSG_THREADS	"You have allocated more cores than are available.  This is likely to GREATLY REDUCE performance.  Do you want to correct this?"
#define MSG_100M	"The 100 million digit work preference requires setting per-worker temporary disk space to 12GB or more.  Work preference changed to first-time primality testing."
#define MSG_FIRST	"The first time prime test work preference may require setting per-worker temporary disk space to at least 1.5GB."
#define MSG_DISK	"Setting temporary disk space below 1.5GB may preclude getting first time prime tests from the PrimeNet server."

/* Common routines */

void sanitizeString (char *);
void rangeStatusMessage (char *, unsigned int);
int min_cores_for_work_pref (int work_pref);
