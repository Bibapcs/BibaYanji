/*
 * Copyright 1995-2020 Mersenne Research, Inc.  All rights reserved
 *
 * Common routines and variables used by Prime95 and NTPrime
 *
 * Comm95b contains information used only during execution
 * Comm95c contains information used during setup and execution
 */ 

#include <wininet.h>
#include <process.h>
#include <sddl.h>

/* Common routines */

/* Load the PrimeNet DLL, make sure an internet connection is active */

int LoadPrimeNet (void)
{
	DWORD	flags;

/* If we're not using a dial-up connection, let primenet try to contact the server. */

	if (!DIAL_UP) return (TRUE);

/* RAS calls, see below, is no longer the MS-prefered method of detecting */
/* an Internet connection.  Starting in version 22.10 we offer a way for */
/* for users to use the prefered wininet.dll method. */
/* InternetGetConnectedState should return FALSE if the modem is not */
/* connected to the Internet. */
/* Starting in version 25.1, this became the default detection method. */
/* Starting with version 29.1 this became the only supported detection method */

	if (InternetGetConnectedState (&flags, 0)) return (TRUE);

// Print error message if no there are no connections

	OutputStr (COMM_THREAD_NUM, "Dial-up connection not active.\n");
	return (FALSE);
}

/* Unload the PrimeNet DLL */

void UnloadPrimeNet (void)
{
}

/* Get Windows Serial Number */

void getWindowsSerialNumber (
	char	*output)
{
	HKEY	hkey = 0;
	char	buf[256];
	DWORD	type, disposition;
	DWORD	bufsize = sizeof (buf);

	*output = 0;
	if (RegCreateKeyEx (
			HKEY_LOCAL_MACHINE,
			"Software\\Microsoft\\Windows\\CurrentVersion",
			0,
			NULL,
			REG_OPTION_NON_VOLATILE,
			KEY_ALL_ACCESS,
			NULL,
			&hkey,
			&disposition) == ERROR_SUCCESS &&
	    RegQueryValueEx (hkey, "ProductId", NULL, &type,
			(BYTE *) buf, &bufsize) == ERROR_SUCCESS &&
	    type == REG_SZ)
		strcpy (output, buf);
	if (hkey) RegCloseKey (hkey);
}

/* Get Windows Serial Number - our second attempt.  This is more robust */
/* as it works under Vista too. */

void getWindowsSerialNumber_2 (
	char	*output)
{
	HKEY	hkey;
	char	buf[256];
	DWORD	type;
	DWORD	bufsize;

	*output = 0;

	hkey = 0;
	bufsize = sizeof (buf);
	if (RegOpenKeyEx (HKEY_LOCAL_MACHINE,
			  "Software\\Microsoft\\Windows\\CurrentVersion", 0,
			  KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS &&
	    RegQueryValueEx (hkey, "ProductId", NULL, &type,
			     (BYTE *) buf, &bufsize) == ERROR_SUCCESS &&
	    type == REG_SZ)
		strcpy (output, buf);
	if (hkey) RegCloseKey (hkey);

	if (*output) return;

	hkey = 0;
	bufsize = sizeof (buf);
	if (RegOpenKeyEx (HKEY_LOCAL_MACHINE,
			  "Software\\Microsoft\\Windows NT\\CurrentVersion", 0,
			  KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS &&
	    RegQueryValueEx (hkey, "ProductId", NULL, &type,
			     (BYTE *) buf, &bufsize) == ERROR_SUCCESS &&
	    type == REG_SZ)
		strcpy (output, buf);
	if (hkey) RegCloseKey (hkey);
}

/* Get Window's SID.  Hopefully this will combined with the Window's */
/* serial # will generate a unique computer ID.  The SID code */
/* came courtesy of www.sysinternals.com with this copyright */
/* and restriction. */
// Copyright (c) 1997-2002 Mark Russinovich and Bryce Cogswell
//
// Changes the computer SID. 
//
// This code is protected under copyright law. You do not have 
// permission to use this code in a commercial SID-changing product.

PSECURITY_DESCRIPTOR GetRegSecDesc (HKEY Root, TCHAR *Path, 
				    SECURITY_INFORMATION Information)
{
	HKEY					hKey;
	LONG					Status;
	DWORD					nb = 0;
	PSECURITY_DESCRIPTOR	SecDesc;

	//
	// Open the key with no access requests, since we don't need
	// any.
	// SECURITY_DESCRIPTOR
	if (RegOpenKeyEx (Root, Path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return NULL;

	//
	// Grab a copy of the security for key
	//
	if (RegGetKeySecurity (hKey, Information, NULL, &nb) 
					!= ERROR_INSUFFICIENT_BUFFER)
		return NULL;

	SecDesc = malloc (nb);
	Status = RegGetKeySecurity (hKey, Information, SecDesc, &nb);

	//
	// Close the key anyway
	//
	RegCloseKey (hKey);
	if (Status != ERROR_SUCCESS) {
		free (SecDesc);
		return NULL;
	}
	return SecDesc;
}
PSECURITY_DESCRIPTOR GetRegAccess (HKEY hKey)
{
	DWORD		nb = 0;
	PSECURITY_DESCRIPTOR SecDesc;
	//
	// Get access
	//
	if (RegGetKeySecurity (hKey, DACL_SECURITY_INFORMATION, NULL, &nb) != ERROR_INSUFFICIENT_BUFFER)
		return NULL;
	SecDesc = (PSECURITY_DESCRIPTOR) malloc (nb);
	if (RegGetKeySecurity (hKey, DACL_SECURITY_INFORMATION, SecDesc, &nb) != ERROR_SUCCESS) {
		free (SecDesc);
		return (NULL);
	}
	return (SecDesc);
}
LONG SetRegAccess (HKEY hKey, LPCTSTR lpSubKey,
		   PSECURITY_DESCRIPTOR SecDesc, PHKEY phKey)
{
	//
	// Grant requested access
	//
	if (RegSetKeySecurity (*phKey, DACL_SECURITY_INFORMATION, SecDesc) 
					!= ERROR_SUCCESS)
		return FALSE;

	//
	// Re-open the key if requested
	//
	if (! hKey) return TRUE;

	RegCloseKey (*phKey);
	return (RegOpenKey (hKey, lpSubKey, phKey) == ERROR_SUCCESS);
}
PBYTE IsSubAuthValid( PBYTE SidData, DWORD SidLength )
{
	PBYTE	sidPtr;

	sidPtr = NULL;
	if ( SidLength % sizeof(DWORD) == 0 )  {
		for ( sidPtr = SidData + SidLength - 5*sizeof(DWORD); sidPtr >= SidData; sidPtr -= sizeof(DWORD) )
			if ( ((PDWORD)sidPtr)[1] == 0x05000000  &&  ((PDWORD)sidPtr)[2] == 0x00000015 )
				break;
		if ( sidPtr < SidData )
			sidPtr = NULL;
	}
	return sidPtr;
}
void GetTextualSid(
    PSID pSid,            // binary Sid
    PTCHAR TextualSid)    // buffer for Textual representation of Sid
{
    PSID_IDENTIFIER_AUTHORITY psia;
    DWORD dwSubAuthorities;
    DWORD dwSidRev=SID_REVISION;
    DWORD dwCounter;
    DWORD dwSidSize;

    // Validate the binary SID.

    if(!IsValidSid(pSid)) return;

    // Get the identifier authority value from the SID.

    psia = GetSidIdentifierAuthority(pSid);

    // Get the number of subauthorities in the SID.

    dwSubAuthorities = *GetSidSubAuthorityCount(pSid);

    // Compute the buffer length.
    // S-SID_REVISION- + IdentifierAuthority- + subauthorities- + NULL

    dwSidSize=(15 + 12 + (12 * dwSubAuthorities) + 1) * sizeof(TCHAR);

    // Add 'S' prefix and revision number to the string.

    dwSidSize= _stprintf(TextualSid, _T("S-%lu-"), dwSidRev );

    // Add SID identifier authority to the string.

    if ( (psia->Value[0] != 0) || (psia->Value[1] != 0) ) {

        dwSidSize += _stprintf(TextualSid + lstrlen(TextualSid),
                    _T("0x%02hx%02hx%02hx%02hx%02hx%02hx"),
                    (USHORT)psia->Value[0],
                    (USHORT)psia->Value[1],
                    (USHORT)psia->Value[2],
                    (USHORT)psia->Value[3],
                    (USHORT)psia->Value[4],
                    (USHORT)psia->Value[5]);

    } else {

        dwSidSize += _stprintf(TextualSid + lstrlen(TextualSid),
                     _T("%lu"),
                    (ULONG)(psia->Value[5]      )   +
                    (ULONG)(psia->Value[4] <<  8)   +
                    (ULONG)(psia->Value[3] << 16)   +
                    (ULONG)(psia->Value[2] << 24)   );
    }

    // Add SID subauthorities to the string.
    //
    for (dwCounter=0 ; dwCounter < dwSubAuthorities ; dwCounter++) {
        dwSidSize+= _stprintf(TextualSid + dwSidSize, _T("-%lu"),
                    *GetSidSubAuthority(pSid, dwCounter) );
    }
}
void getWindowsSID (
	char	*output)
{
	PSECURITY_DESCRIPTOR	newSecDesc, oldSecDesc;
	DWORD					valType;
	PBYTE					vData;
	HKEY					hKey;
	DWORD					nb;
	PBYTE					sidPtr;
	DWORD					Status;

	*output = 0;
	//
	// Now, get the descriptor of HKLM\SOFTWARE and apply this to SECURITY
	//
	newSecDesc = GetRegSecDesc( HKEY_LOCAL_MACHINE, "SOFTWARE",
					DACL_SECURITY_INFORMATION );

	//
	// Read the last subauthority of the current computer SID
	//
	if( RegOpenKey( HKEY_LOCAL_MACHINE, "SECURITY\\SAM\\Domains\\Account", 
			&hKey) != ERROR_SUCCESS ) {
		free (newSecDesc);
		return;
	}
	oldSecDesc = GetRegAccess( hKey );
	SetRegAccess( HKEY_LOCAL_MACHINE, "SECURITY\\SAM\\Domains\\Account", 
		newSecDesc, &hKey );
	nb = 0;
	vData = NULL;
	RegQueryValueEx( hKey, "V", NULL, &valType, vData, &nb );
	vData = (PBYTE) malloc( nb );
	Status = RegQueryValueEx( hKey, "V", NULL, &valType, vData, &nb );
	if( Status != ERROR_SUCCESS ) {
		SetRegAccess( HKEY_LOCAL_MACHINE, "SECURITY\\SAM\\Domains\\Account",
				oldSecDesc, &hKey );
		free (vData);
		free (oldSecDesc);
		free (newSecDesc);
		return;
	}
	SetRegAccess( NULL, NULL, oldSecDesc, &hKey );
	RegCloseKey( hKey );
	free (oldSecDesc);
	free (newSecDesc);

	//
	// Make sure that we're dealing with a SID we understand
	//
	if( !(sidPtr = IsSubAuthValid( vData, nb ))) {
		free (vData);
		return;
	}

	GetTextualSid( sidPtr, output );
	free (vData);
}

/* Version 2.  My simpler attempt at getting the Windows SID. */
/* Version 1 failed on RegOpenKey when I switch users on Vista. */

void getWindowsSID_2 (
	char	*output)
{
	char	computer_name[256];
	SID	*sid;
	SID_NAME_USE snu;
	char	*domain;
	char	*stringsid;
	unsigned long size, domainsize;

	*output = 0;

/* Get the computer name */

	size = sizeof (computer_name);
	if (! GetComputerName ((LPSTR) computer_name, &size)) return;

/* First find the size of buffers required for the SID and domain name */

	sid = 0;
	domain = 0;
	size = domainsize = 0;
	LookupAccountName(0, (LPCSTR) computer_name, sid, &size, domain, &domainsize, &snu);

/* Should have failed with ERROR_INSUFFICIENT_BUFFER */

	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return;

/* Allocate memory */

	sid = (SID *) malloc (size);
	domain = (char *) malloc (domainsize);

/* Get the SID */

	if (sid != NULL && domain != NULL &&
	    LookupAccountName (0, (LPCSTR) computer_name, sid, &size, domain, &domainsize, &snu) &&
	    ConvertSidToStringSid(sid, &stringsid)) {
		strcpy (output, stringsid);
		LocalFree (stringsid);
	}

/* Cleanup */

	free (sid);
	free (domain);
}


/* Return the number of MB of physical memory. */

unsigned long physical_memory (void)
{
	MEMORYSTATUSEX mem;
	mem.dwLength = sizeof (mem);
	GlobalMemoryStatusEx (&mem);
	return ((unsigned long) ((mem.ullTotalPhys + 1000000) >> 20));
}

/* Return a better guess for amount of memory to use in a torture test. */
/* Caller passes in its guess for amount of memory to use, but this routine */
/* can reduce that guess based on OS-specific code that looks at amount */
/* of available physical memory. */
/* This code was written by an anonymous GIMPS user. */

unsigned long GetSuggestedMemory (unsigned long nDesiredMemory)
{
	MEMORYSTATUSEX ms = {0};
	DWORDLONG ullUsedMem;	// In-use Physical RAM in bytes
	DWORDLONG ullDesiredMem = (DWORDLONG) nDesiredMemory << 20;	// Desired memory in bytes
	DWORDLONG ullDesiredMemNew = ullDesiredMem;

	ms.dwLength = sizeof (ms);
	GlobalMemoryStatusEx (&ms);
	ullUsedMem = ms.ullTotalPhys - ms.ullAvailPhys;

	// if very small/no page-file (pagefile <= total RAM) and
	// in-use memory + desired memory > total RAM, then
	// we have to set desired memory to free RAM,
	// because the OS can't page out other apps to
	// reclaim enough free memory since
	// there's not enough space in the pagefile
	// to store the paged-out apps
	if ((ms.ullTotalPageFile <= ms.ullTotalPhys) &&
	    (ullUsedMem + ullDesiredMem >= ms.ullTotalPhys)) {
		ullDesiredMemNew = ms.ullAvailPhys;
	}

	return ((unsigned long) (ullDesiredMemNew >> 20));
}

/* Return 1 to print time in AM/PM format.  Return 2 to print */
/* times using a 24-hour clock. */

int getDefaultTimeFormat (void)
{
	char	buf[10];

	GetLocaleInfo (
		LOCALE_USER_DEFAULT, LOCALE_ITIME, (LPTSTR) buf, sizeof (buf));
	return (buf[0] == '0' ? 1 : 2);
}

/* Return TRUE if we are on battery power. */

int OnBattery (void)
{
	SYSTEM_POWER_STATUS power;

// We might be able to optimize this by caching the system power status
// and only regetting it after a PBT_APMPOWERSTATUSCHANGE message.

	if (GetSystemPowerStatus (&power) &&
	    (power.ACLineStatus != 1 ||
	     (power.ACLineStatus == 1 &&
	      power.BatteryLifePercent < BATTERY_PERCENT)))
		return (TRUE);

// Return FALSE, were on AC power */

	return (FALSE);
}

/* Get list of file names from the current working directory ending in .proof */

int ProofFileNames (char filenames[50][255])	// Returns number of matching filenames
{
	HANDLE hFind;
	WIN32_FIND_DATA FindFileData;
	int	num_files;

	num_files = 0;
	if ((hFind = FindFirstFile ("*.proof", &FindFileData)) != INVALID_HANDLE_VALUE) do {
		// Double-check the pattern matching (to match Linux code)
		int	len = (int) strlen (FindFileData.cFileName);
		if (len > 6 && strcmp (FindFileData.cFileName + len - 6, ".proof") == 0) {
			if (num_files < 50) strcpy (filenames[num_files++], FindFileData.cFileName);
		}
	} while (FindNextFile (hFind, &FindFileData));
	FindClose (hFind);
	return (num_files);
}

/* Get the character that separates directories in a pathname */

char getDirectorySeparator ()
{
	return ('\\');
}

