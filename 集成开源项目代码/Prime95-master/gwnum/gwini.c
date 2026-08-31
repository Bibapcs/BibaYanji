/*----------------------------------------------------------------------
| gwini.c
|
| This file contains portable routines to read and write "INI" files.  These
| routines came from prime95.  Thus, there are a few idiosyncracies that may
| not be useful to a wide audience.
|
| NOTE:  These routines only work if you open no more than 10 ini files.  Also,
| you must not change the working directory at any time during program execution.
|
| Copyright 2016-2023 Mersenne Research, Inc.  All rights reserved
+---------------------------------------------------------------------*/

/* Include files */

#ifdef _WIN32
#include <io.h>
#include <malloc.h>
#else
#include <unistd.h>
#include <strings.h>
#define _O_APPEND	O_APPEND
#define _O_RDONLY	O_RDONLY
#define _O_WRONLY	O_WRONLY
#define _O_RDWR		O_RDWR
#define _O_CREAT	O_CREAT
#define _O_TRUNC	O_TRUNC
#define _O_BINARY 	0
#define _O_TEXT		0
#define _open		open
#define _close		close
#define _read		read
#define _write		write
#define _unlink		unlink
#define _stricmp	strcasecmp
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "gwcommon.h"
#include "gwini.h"
#include "gwthread.h"
#include "gwutil.h"

/* Define the world/group/owner read/write attributes for creating files */
/* I've always used 0666 in Unix (everyone gets R/W access), but MSVC 8 */
/* now refuses to work with that setting -- insisting on 0600 instead. */

#ifdef _WIN32
#define	CREATE_FILE_ACCESS	0600
#else
#define	CREATE_FILE_ACCESS	0666
#endif

#ifndef _O_CREAT
#define _O_APPEND	O_APPEND
#define _O_RDONLY	O_RDONLY
#define _O_WRONLY	O_WRONLY
#define _O_RDWR		O_RDWR
#define _O_CREAT	O_CREAT
#define _O_TRUNC	O_TRUNC
#define _O_BINARY 	O_BINARY
#define _O_TEXT		O_TEXT
#endif

/* Structures used in managing INI files */

#define INI_LINE_NORMAL		0	/* A normal keyword=value INI line */
#define INI_LINE_COMMENT	2	/* A comment line */
#define INI_LINE_HEADER		3	/* A section header line */

struct IniLine {
	char	*keyword;
	char	*value;
	int	line_type;
};

struct IniCache {
	char	*filename;
	int	immediate_writes;
	int	dirty;
	unsigned int num_lines;
	unsigned int array_size;
	struct IniLine **lines;
};

/* Global variables */

gwmutex	INI_MUTEX = NULL;		/* Lock for accessing INI files */
gwmutex	INI_ADD_MUTEX = NULL;		/* Lock for accessing INI add-in files */
void (*INI_ERROR_CALLBACK)(const char *, int, const char *);	/* Callback routine when illegal line read from INI file. */
								/* Arguments are file name, line number, text on the line */

/* Forward declarations of internal routines */

struct IniCache *openIniFile (const char *, int);
void growIniLineArray (struct IniCache *);
void parse_timed_ini_value (const char *, unsigned int *, unsigned int *, unsigned int *);


/****************************************************************************/
/*              Routines to read and write INI files                        */
/****************************************************************************/

void IniFileReread (			/* Read or reread an INI file */
	const char *filename)
{
	struct IniCache *p;

/* Read the ini file while locking out other callers */

	if (INI_MUTEX == NULL) gwmutex_init (&INI_MUTEX);
	gwmutex_lock (&INI_MUTEX);
	p = openIniFile (filename, 1);
	gwmutex_unlock (&INI_MUTEX);
}

void writeIniFile (			/* Write a changed INI file to disk */
	struct IniCache *p)
{
	int	fd;
	unsigned int j;
	char	buf[2000];

/* Delay writing the file unless this INI file is written */
/* to immediately */

	if (!p->immediate_writes) {
		p->dirty = 1;
		return;
	}

/* Create and write out the INI file */

	fd = _open (p->filename, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_TEXT, CREATE_FILE_ACCESS);
	if (fd < 0) return;
	for (j = 0; j < p->num_lines; j++) {
		if (p->lines[j]->line_type == INI_LINE_COMMENT) {
			strcpy (buf, p->lines[j]->value);
		} else if (p->lines[j]->line_type == INI_LINE_HEADER) {
			strcpy (buf, p->lines[j]->value);
		} else {
			strcpy (buf, p->lines[j]->keyword);
			strcat (buf, "=");
			strcat (buf, p->lines[j]->value);
		}
		strcat (buf, "\n");
		(void) _write (fd, buf, (unsigned int) strlen (buf));
	}
	p->dirty = 0;
	_close (fd);
}

/* Delay writing changes to the INI file */

void IniDelayWrites (
	const char *filename)
{
	struct IniCache *p;
	p = openIniFile (filename, 0);
	p->immediate_writes = FALSE;
}

/* Resume immediately writing changes to the INI file */

void IniResumeImmediateWrites (
	const char *filename)
{
	struct IniCache *p;
	p = openIniFile (filename, 0);
	p->immediate_writes = TRUE;
	if (p->dirty) writeIniFile (p);
}

/* Merge one "add file" into an ini file.  Assumes the ini file has been */
/* freshly re-read from disk.  This can also is used to copy one ini file */
/* into a section of another ini file. */

void IniAddFileMerge (
	const char *ini_filename,
	const char *add_filename,
	const char *section_to_copy_to)
{
	struct IniCache *p, *q;
	const char *section;
	unsigned int j;

/* Obtain a lock so that only one thread adds to the INI file.  We will release */
/* lock once we've deleted the add file. */

	if (INI_ADD_MUTEX == NULL) gwmutex_init (&INI_ADD_MUTEX);
	gwmutex_lock (&INI_ADD_MUTEX);

/* Open ini files */

	p = openIniFile (ini_filename, 0);
	q = openIniFile (add_filename, 1);

/* Save up all the writes */

	p->immediate_writes = FALSE;

/* Loop through all the lines in the add file, adding them to the */
/* base ini file */

	section = section_to_copy_to;
	for (j = 0; j < q->num_lines; j++) {
		if (q->lines[j]->line_type == INI_LINE_HEADER) {
			if (section_to_copy_to == NULL)
				section = q->lines[j]->keyword;
		}
		else if (q->lines[j]->line_type != INI_LINE_COMMENT)
			IniSectionWriteString (ini_filename, section, q->lines[j]->keyword, q->lines[j]->value);
	}

/* Output all the saved up writes */

	p->immediate_writes = TRUE;
	writeIniFile (p);

/* Delete the add file */

	_unlink (add_filename);

/* Unlock and return */

	gwmutex_unlock (&INI_ADD_MUTEX);
}

/****************************************************************************/
/*               Routines to read and write string values                   */
/****************************************************************************/

void IniGetString (			/* Get a string value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val)
{
	IniSectionGetString (filename, NULL, keyword, val, val_bufsize, default_val);
}

void IniSectionGetString (		/* Get a string value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val)
{
	IniSectionGetNthString (filename, section, keyword, 1, val, val_bufsize, default_val);
}

void IniGetNthString (			/* Get keyword's Nth string value from the specified section of the INI file */
	const char *filename,
	const char *keyword,
	int	nth,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val)
{
	IniSectionGetNthString (filename, NULL, keyword, nth, val, val_bufsize, default_val);
}

void IniSectionGetNthString (		/* Get keyword's Nth string value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	int	nth,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val)
{
	unsigned int seconds;
	IniSectionGetNthTimedString (filename, section, keyword, nth, val, val_bufsize, default_val, &seconds);
}

void IniGetTimedString (		/* Get a time-sensitive string value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	IniSectionGetTimedString (filename, NULL, keyword, val, val_bufsize, default_val, seconds);
}

void IniSectionGetTimedString (		/* Get a time-sensitive string value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	IniSectionGetNthTimedString (filename, section, keyword, 1, val, val_bufsize, default_val, seconds);
}

void IniSectionGetNthTimedString (	/* Get keyword's Nth time-sensitive string value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	int	nth,
	char	*val,
	unsigned int val_bufsize,
	const char *default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	const char *p;
	unsigned int start, len;

/* Lookup the keyword */

	p = IniSectionGetNthStringRaw (filename, section, keyword, nth);

/* If we found the keyword in the INI file, then */
/* support different return values based on the time of day. */

	if (p != NULL) {
		parse_timed_ini_value (p, &start, &len, seconds);
		if (len) {
			truncated_strcpy_with_len (val, val_bufsize, p+start, len);
			return;
		}
	} else {
		*seconds = 0;
	}

/* Copy the default value to the caller's buffer */

	if (default_val)
		truncated_strcpy (val, val_bufsize, default_val);
	else
		val[0] = 0;
}

const char *IniSectionGetStringRaw (	/* Return keyword's raw string value from a specific section of the INI file. */
	const char *filename,
	const char *section,
	const char *keyword)
{
	return (IniSectionGetNthStringRaw (filename, section, keyword, 1));
}

const char *IniSectionGetNthStringRaw (	/* Return keyword's Nth raw string value from global section of the INI file. */
	const char *filename,
	const char *section,
	const char *keyword,
	int	nth)			/* Nth occurrence of the keyword (nth starts at 1) */
{
	struct IniCache *p;
	unsigned int i;
	const char *retval;

/* Open ini file */

	if (INI_MUTEX == NULL) gwmutex_init (&INI_MUTEX);
	gwmutex_lock (&INI_MUTEX);
	p = openIniFile (filename, 0);

/* Skip to the correct section */

	i = 0;
	if (section != NULL) {
		for ( ; i < p->num_lines; i++) {
			if (p->lines[i]->line_type == INI_LINE_HEADER &&
			    _stricmp (section, p->lines[i]->keyword) == 0) {
				i++;
				break;
			}
		}
	}

/* Look for the keyword within this section */

	for ( ; ; i++) {
		if (i == p->num_lines ||
		    p->lines[i]->line_type == INI_LINE_HEADER) {
			retval = NULL;
			break;
		}
		if (p->lines[i]->line_type == INI_LINE_NORMAL &&
		    _stricmp (keyword, p->lines[i]->keyword) == 0 &&
		    --nth == 0) {
			retval = p->lines[i]->value;
			break;
		}
	}

/* Unlock and return */

	gwmutex_unlock (&INI_MUTEX);
	return (retval);
}

void IniWriteString (			/* Write a string value to the global section of the INI file. */
	const char *filename,
	const char *keyword,
	const char *val)
{
	IniSectionWriteString (filename, NULL, keyword, val);
}

void IniSectionWriteString (		/* Write a string value to a specified section of the INI file. */
	const char *filename,
	const char *section,
	const char *keyword,
	const char *val)
{
	IniSectionWriteNthString (filename, section, keyword, 1, val);
}

void IniWriteNthString (		/* Write keyword's Nth string value to a specified section of the INI file. */
	const char *filename,
	const char *keyword,
	int	nth,
	const char *val)		/* New value.  If NULL, delete all occurrences of keyword entry */
{
	IniSectionWriteNthString (filename, NULL, keyword, nth, val);
}

void IniSectionWriteNthString (		/* Write keyword's Nth string value to a specified section of the INI file. */
	const char *filename,
	const char *section,
	const char *keyword,		/* If NULL, insert a comment line */
	int	nth,
	const char *val)		/* New value.  If NULL, delete all occurrences of keyword entry */
{
	struct IniCache *p;
	unsigned int i, j, insertion_point;
	int	lines_were_deleted;

/* Open ini file */

	if (INI_MUTEX == NULL) gwmutex_init (&INI_MUTEX);
	gwmutex_lock (&INI_MUTEX);
	p = openIniFile (filename, 0);

/* Skip to the correct section.  If the section does not exist, create it */

	i = 0;
	if (section != NULL) {
		for ( ; ; i++) {
			if (i == p->num_lines) {
				if (val == NULL) goto nowrite_done;
				growIniLineArray (p);
				p->lines[i] = (struct IniLine *) malloc (sizeof (struct IniLine));
				p->lines[i]->line_type = INI_LINE_COMMENT;
				p->lines[i]->keyword = NULL;
				p->lines[i]->value = (char *) malloc (1);
				p->lines[i]->value[0] = 0;
				p->num_lines++;
				i++;
				growIniLineArray (p);
				p->lines[i] = (struct IniLine *) malloc (sizeof (struct IniLine));
				p->lines[i]->line_type = INI_LINE_HEADER;
				p->lines[i]->keyword = (char *) malloc (strlen (section) + 1);
				strcpy (p->lines[i]->keyword, section);
				p->lines[i]->value = (char *) malloc (strlen (section) + 3);
				sprintf (p->lines[i]->value, "[%s]", section);
				p->num_lines++;
				i++;
				break;
			}
			if (p->lines[i]->line_type == INI_LINE_HEADER && _stricmp (section, p->lines[i]->keyword) == 0) {
				i++;
				break;
			}
		}
	}

/* Look for the keyword within this section */

	insertion_point = i;
	lines_were_deleted = FALSE;
	for ( ; ; i++) {
		if (i == p->num_lines || p->lines[i]->line_type == INI_LINE_HEADER ||
		    (p->lines[i]->line_type != INI_LINE_COMMENT && _stricmp (p->lines[i]->keyword, "Time") == 0)) {

/* Ignore request if we are deleting line */

			if (val == NULL) {
				if (lines_were_deleted) goto write_done;
				goto nowrite_done;
			}

/* Make sure the line array has room for the new line */

			growIniLineArray (p);

/* Shuffle entries down to make room for this entry */

			i = insertion_point;
			for (j = p->num_lines; j > i; j--) p->lines[j] = p->lines[j-1];

/* Allocate and fill in a new line structure */

			p->lines[i] = (struct IniLine *) malloc (sizeof (struct IniLine));
			if (keyword == NULL) {
				p->lines[i]->line_type = INI_LINE_COMMENT;
				p->lines[i]->keyword = NULL;
			} else {
				p->lines[i]->line_type = INI_LINE_NORMAL;
				p->lines[i]->keyword = (char *) malloc (strlen (keyword) + 1);
				strcpy (p->lines[i]->keyword, keyword);
			}
			p->lines[i]->value = NULL;
			p->num_lines++;
			break;
		}

/* If this is not a blank line, then if we need to insert a new line, insert it after this line. */
/* In other words, insert new entries before any blank lines at the end of a section. */

		if (p->lines[i]->line_type != INI_LINE_COMMENT || p->lines[i]->value[0]) {
			insertion_point = i + 1;
		}

/* If this is the keyword we are looking for, then we will replace the value if it has changed. */

		if (p->lines[i]->line_type == INI_LINE_NORMAL && keyword != NULL && _stricmp (keyword, p->lines[i]->keyword) == 0) {

/* Delete the entry (all occurrences) if there is no new value */

			if (val == NULL) {

/* Free the data associated with the given line */

				free (p->lines[i]->keyword);
				free (p->lines[i]->value);
				free (p->lines[i]);

/* Delete the line from the lines array */

				for (j = i + 1; j < p->num_lines; j++) p->lines[j-1] = p->lines[j];
				p->num_lines--;
				i--;
				lines_were_deleted = TRUE;
			}

/* Replace the entry if this is the nth occurrence */

			else if (--nth == 0) {
				if (strcmp (val, p->lines[i]->value) == 0) goto nowrite_done;
				break;
			}
		}
	}

/* Replace the value associated with the keyword */

	if (val != NULL) {
		free (p->lines[i]->value);
		p->lines[i]->value = (char *) malloc (strlen (val) + 1);
		strcpy (p->lines[i]->value, val);
	}

/* Write the INI file back to disk */

write_done:
	writeIniFile (p);

/* Unlock and return */

nowrite_done:
	gwmutex_unlock (&INI_MUTEX);
}

/****************************************************************************/
/*               Routines to read and write integer values                  */
/****************************************************************************/

long IniGetInt (			/* Get an integer value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	long	default_val)
{
	return (IniSectionGetInt (filename, NULL, keyword, default_val));
}

long IniSectionGetInt (			/* Get an integer value from specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	long	default_val)
{
	unsigned int seconds;
	return (IniSectionGetTimedInt (filename, section, keyword, default_val, &seconds));
}

long IniGetTimedInt (			/* Get a time-sensitive integer value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	long	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	return (IniSectionGetTimedInt (filename, NULL, keyword, default_val, seconds));
}

long IniSectionGetTimedInt (		/* Get a time-sensitive integer value from specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	long	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	char	buf[20], defval[20];
	sprintf (defval, "%ld", default_val);
	IniSectionGetTimedString (filename, section, keyword, buf, 20, defval, seconds);
	return (atol (buf));
}

void IniWriteInt (			/* Write an integer value to the global section of the INI file */
	const char *filename,
	const char *keyword,
	long	val)
{
	IniSectionWriteInt (filename, NULL, keyword, val);
}

void IniSectionWriteInt (		/* Write an integer value to specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	long	val)
{
	char	buf[20];
	sprintf (buf, "%ld", val);
	IniSectionWriteString (filename, section, keyword, buf);
}

int64_t IniGetInt64 (			/* Get an integer value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	int64_t	default_val)
{
	return (IniSectionGetInt64 (filename, NULL, keyword, default_val));
}

int64_t IniSectionGetInt64 (		/* Get an integer value from specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	int64_t	default_val)
{
	unsigned int seconds;
	return (IniSectionGetTimedInt64 (filename, section, keyword, default_val, &seconds));
}

int64_t IniGetTimedInt64 (		/* Get a time-sensitive integer value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	int64_t	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	return (IniSectionGetTimedInt64 (filename, NULL, keyword, default_val, seconds));
}

int64_t IniSectionGetTimedInt64 (	/* Get a time-sensitive integer value from specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	int64_t	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	char	buf[30], defval[30];
	sprintf (defval, "%" PRIi64, default_val);
	IniSectionGetTimedString (filename, section, keyword, buf, 30, defval, seconds);
	return (atoll (buf));
}

void IniWriteInt64 (			/* Write an integer value to the global section of the INI file */
	const char *filename,
	const char *keyword,
	int64_t	val)
{
	IniSectionWriteInt64 (filename, NULL, keyword, val);
}

void IniSectionWriteInt64 (		/* Write an integer value to specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	int64_t	val)
{
	char	buf[30];
	sprintf (buf, "%" PRIi64, val);
	IniSectionWriteString (filename, section, keyword, buf);
}

/****************************************************************************/
/*                Routines to read and write float values		    */
/****************************************************************************/

float IniGetFloat (			/* Get a floating point value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	float	default_val)
{
	return (IniSectionGetFloat (filename, NULL, keyword, default_val));
}

float IniSectionGetFloat (		/* Get a floating point value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	float	default_val)
{
	unsigned int seconds;
	return (IniSectionGetTimedFloat (filename, section, keyword, default_val, &seconds));
}

float IniGetTimedFloat (		/* Get a time-sensitive floating point value from the global section of the INI file */
	const char *filename,
	const char *keyword,
	float	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	return (IniSectionGetTimedFloat (filename, NULL, keyword, default_val, seconds));
}

float IniSectionGetTimedFloat (		/* Get a time-sensitive floating point value from the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	float	default_val,
	unsigned int *seconds)		/* Return length of time this timed INI setting is good for. */
{
	char	buf[20], defval[20];
	sprintf (defval, "%f", default_val);
	IniSectionGetTimedString (filename, section, keyword, buf, 20, defval, seconds);
	return ((float) atof (buf));
}

void IniWriteFloat (			/* Write a floating point value to the global section of the INI file */
	const char *filename,
	const char *keyword,
	float	val)
{
	IniSectionWriteFloat (filename, NULL, keyword, val);
}

void IniSectionWriteFloat (		/* Write a floating point value to the specified section of the INI file */
	const char *filename,
	const char *section,
	const char *keyword,
	float	val)
{
	/* Assume FLT_MAX is 3.40282e+038, the maximum significant digits that */
	/* can be stored in this buf is 12. ((sizeof(buf))-sizeof("-.E+038")) */
 	char	buf[20];
	sprintf (buf, "%g", val);
 	IniSectionWriteString (filename, section, keyword, buf);
}

/****************************************************************************/
/*                           Internal routines             		    */
/****************************************************************************/

/* Open and read an INI file */

struct IniCache *openIniFile (
	const char *filename,
	int	forced_read)
{
static	struct IniCache *cache[10] = {0};
	struct IniCache *p;
	FILE	*fd;
	unsigned int i;
	char	line[1024];
	char	*val;

/* See if file is cached */

	for (i = 0; i < 10; i++) {
		p = cache[i];
		if (p == NULL) {
			p = (struct IniCache *) malloc (sizeof (struct IniCache));
			p->filename = (char *) malloc (strlen (filename) + 1);
			strcpy (p->filename, filename);
			p->immediate_writes = 1;
			p->dirty = 0;
			p->num_lines = 0;
			p->array_size = 0;
			p->lines = NULL;
			forced_read = 1;
			cache[i] = p;
			break;
		}
		if (strcmp (filename, p->filename) == 0)
			break;
	}

/* Skip reading the ini file if appropriate */

	if (!forced_read) return (p);
	if (p->dirty) return (p);

/* Free the data if we've already read some in */

	for (i = 0; i < p->num_lines; i++) {
		free (p->lines[i]->keyword);
		free (p->lines[i]->value);
		free (p->lines[i]);
	}
	p->num_lines = 0;

/* Read the IniFile */

	fd = fopen (filename, "r");
	if (fd == NULL) return (p);

	while (fgets (line, sizeof (line), fd)) {
		if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
		if (line[0] && line[strlen(line)-1] == '\r') line[strlen(line)-1] = 0;

/* Allocate and fill in a new line structure */

		growIniLineArray (p);
		i = p->num_lines++;
		p->lines[i] = (struct IniLine *) malloc (sizeof (struct IniLine));

/* Flag section headers */

		if (line[0] == '[') {
			char	*q;

			p->lines[i]->keyword = (char *) malloc (strlen (line) + 1);
			p->lines[i]->value = (char *) malloc (strlen (line) + 1);
			p->lines[i]->line_type = INI_LINE_HEADER;
			strcpy (p->lines[i]->value, line);
			strcpy (p->lines[i]->keyword, line+1);
			for (q = p->lines[i]->keyword; *q; q++)
				if (*q == ']') {
					*q = 0;
					break;
				}
		}

/* Save comment lines - any line that doesn't begin with a letter */

		else if ((line[0] < 'A' || line[0] > 'Z') &&
			 (line[0] < 'a' || line[0] > 'z')) {
			p->lines[i]->keyword = NULL;
			p->lines[i]->value = (char *) malloc (strlen (line) + 1);
			p->lines[i]->line_type = INI_LINE_COMMENT;
			strcpy (p->lines[i]->value, line);
		}

/* Otherwise, parse keyword=value lines */

		else {
			val = strchr (line, '=');
			if (val == NULL) {
				/* Unparseable line, call user error handling routine */
				if (INI_ERROR_CALLBACK != NULL) (*INI_ERROR_CALLBACK)(filename, p->num_lines, line);
				/* Save unparseable line as a comment */
				p->lines[i]->keyword = NULL;
				p->lines[i]->value = (char *) malloc (strlen (line) + 1);
				p->lines[i]->line_type = INI_LINE_COMMENT;
				strcpy (p->lines[i]->value, line);
			} else {
				*val++ = 0;
				p->lines[i]->keyword = (char *) malloc (strlen (line) + 1);
				p->lines[i]->value = (char *) malloc (strlen (val) + 1);
				p->lines[i]->line_type = INI_LINE_NORMAL;
				strcpy (p->lines[i]->keyword, line);
				strcpy (p->lines[i]->value, val);
			}
		}
	}
	fclose (fd);

	return (p);
}

void growIniLineArray (
	struct IniCache *p)
{
	struct IniLine **newlines;

	if (p->num_lines != p->array_size) return;

	newlines = (struct IniLine **) malloc ((p->num_lines + 100) * sizeof (struct IniLine *));
	if (p->num_lines) {
		memcpy (newlines, p->lines, p->num_lines * sizeof (struct IniLine *));
		free (p->lines);
	}
	p->lines = newlines;
	p->array_size = p->num_lines + 100;
}

/* Routines to help analyze a timed line in an INI file */

void parseTimeLine (
	const char **line,
	int	*start_day,
	int	*end_day,
	int	*start_time,
	int	*end_time)
{
	const char *p;

/* Get the days of the week, e.g. 1-5 */

	p = *line;
	*start_day = atoi (p); while (isdigit (*p)) p++;
	if (*p == '-') {
		p++;
		*end_day = atoi (p); while (isdigit (*p)) p++;
	} else
		*end_day = *start_day;

/* Now do time portion.  If none present, then assume the numbers we */
/* parsed above were times, not days of the week. */

	if (*p == '/')
		p++;
	else {
		p = *line;
		*start_day = 1;
		*end_day = 7;
	} 
	*start_time = atoi (p) * 60; while (isdigit (*p)) p++;
	if (*p == ':') {
		p++;
		*start_time += atoi (p); while (isdigit (*p)) p++;
	}
	if (*p == '-') p++;			/* Skip '-' */
	*end_time = atoi (p) * 60; while (isdigit (*p)) p++;
	if (*p == ':') {
		p++;
		*end_time += atoi (p); while (isdigit (*p)) p++;
	}

/* Return ptr to next time interval on the line */

	if (*p++ == ',') *line = p;
	else *line = NULL;
}

int analyzeTimeLine (
	const char *line,
	time_t	current_t,
	unsigned int *wakeup_time)
{
	struct tm *x;
	int	current_time;
	const char *p;
	int	day, start_day, end_day, start_time, end_time;
	int	full_start_time, full_end_time;
	int	wakeup_t, min_wakeup_t;

/* Break current time into a more easily maniupulated form */

	x = localtime (&current_t);
	current_time = (x->tm_wday ? x->tm_wday : 7) * 24 * 60;
	current_time += x->tm_hour * 60 + x->tm_min;

/* Process each interval on the line */

	p = line;
	min_wakeup_t = 0;
	while (p != NULL) {
		parseTimeLine (&p, &start_day, &end_day, &start_time, &end_time);

/* Treat each day in the range as a separate time interval to process */

		for (day = start_day; day <= end_day; day++) {

/* We allow end_time to be less than start_time.  We treat this as */
/* the next day.  Thus 15:00-01:00 means 3PM to 1AM the next day. */

			full_start_time = day * 24 * 60 + start_time;
			full_end_time = day * 24 * 60 + end_time;
			if (end_time < start_time) full_end_time += 24 * 60;

/* Is the current time in this interval? */

			if (current_time >= full_start_time &&
			    current_time < full_end_time)
				goto winner;

/* Now check for the really sick case, where end_time was less than */
/* start_time and we've wrapped from day 7 back to day 1 */

			if (end_time < start_time && day == 7 &&
			    current_time < full_end_time - 7 * 24 * 60)
				goto winner;

/* No, see if this start time should be our new wakeup time. */

			if (full_start_time >= current_time)
				wakeup_t = (full_start_time - current_time) * 60;
			else
				wakeup_t = (full_start_time + 7 * 24 * 60 - current_time) * 60;
			if (min_wakeup_t == 0 || min_wakeup_t > wakeup_t)
				min_wakeup_t = wakeup_t;
		}
	}

/* Current time was not in any of the intervals */

	*wakeup_time = min_wakeup_t;
	return (FALSE);

/* Current time is in this interval, compute the wakeup time */

winner:	wakeup_t = (full_end_time - current_time) * 60;

/* Also, look for a start time that matches the end time and replace */
/* the end time.  For example, if current time is 18:00 and the */
/* Time= entry is 0:00-8:00,17:00-24:00, then the */
/* end time of 24:00 should be replaced with 8:00 of the next day. */
/* Be sure not to infinite loop in this time entry: 0:00-8:00,8:00-24:00 */

	p = line;
	while (p != NULL && wakeup_t < 10 * 24 * 60) {
		parseTimeLine (&p, &start_day, &end_day, &start_time, &end_time);

/* Treat each day in the range as a separate time interval to process */

		for (day = start_day; day <= end_day; day++) {
			int	this_full_start_time, this_full_end_time;

/* If this start time is the same as the winning end time, then set the new */
/* wakeup time to be the end of this interval.  Be sure to handle the tricky */
/* wrap around that occurs when end_time < start_time. */

			this_full_start_time = day * 24 * 60 + start_time;
			if (this_full_start_time != full_end_time &&
			    this_full_start_time != full_end_time - 7 * 24 * 60) continue;

			this_full_end_time = day * 24 * 60 + end_time;
			if (end_time < start_time) this_full_end_time += 24 * 60;
			wakeup_t += (this_full_end_time - this_full_start_time) * 60;
			full_end_time = this_full_end_time;
			p = line;
			break;
		}
	}

/* Return indicator that current time was covered by one of the intervals */

	*wakeup_time = wakeup_t + 1;
	return (TRUE);
}

/* INI file values can contain be conditional based on the day of the */
/* week and the time of day.  For example, this INI file value is */
/* file has different properties during the work week and weekend. */
/*	Priority=1 during 1-5/8:30-17:30 else 5			*/

void parse_timed_ini_value (
	const char *line,		/* INI value line to analyze */
	unsigned int *start_offset,	/* Returned start offset */
	unsigned int *len,		/* Returned length */
	unsigned int *seconds_valid)	/* Returned length of time */
					/* value is good for */
{
	time_t	current_time;
	const char *rest_of_line, *during_clause, *else_clause;
	unsigned int min_wakeup_time, wakeup_time;

/* Get the current time - so that we compare each timed section */
/* with the same current_time value */

	time (&current_time);

/* Loop processing each timed section in the line */

	rest_of_line = line;
	min_wakeup_time = 0;
	for ( ; ; ) {

/* If we don't see a "during" clause, then either there are no timed sections or we've reached the final else clause.  Return the else clause value. */

		during_clause = strstr (rest_of_line, " during ");
		if (during_clause == NULL) {
			*start_offset = (unsigned int) (rest_of_line - line);
			*len = (unsigned int) strlen (rest_of_line);
			*seconds_valid = min_wakeup_time;
			break;
		}

/* We've got a timed section, if the "during" value is the same as the "else" value then return the during value but don't bother with setting */
/* a new wake up time.  This prevents needless wakeups for memory settings such as "1024 during 2:00-4:00 else 1024". */

		else_clause = strstr (rest_of_line, " else ");
		if (else_clause != NULL &&
		    during_clause - rest_of_line == strlen (else_clause + 6) &&
		    memcmp (rest_of_line, else_clause + 6, during_clause - rest_of_line) == 0) {
			*start_offset = (unsigned int) (rest_of_line - line);
			*len = (unsigned int) (during_clause - rest_of_line);
			*seconds_valid = min_wakeup_time;
			break;
		}

/* We've got a timed section, see if the current time is within this timed section. */

		if (analyzeTimeLine (during_clause+8, current_time, &wakeup_time)) {
			*start_offset = (unsigned int) (rest_of_line - line);
			*len = (unsigned int) (during_clause - rest_of_line);
			*seconds_valid = wakeup_time;
			break;
		}

/* We're not in this timed section, remember which timed section will come into effect first.  This will be the end time of the "else" section. */

		if (min_wakeup_time == 0 || wakeup_time < min_wakeup_time)
			min_wakeup_time = wakeup_time;

/* Move on to the next timed section. */

		if (else_clause != NULL) rest_of_line = else_clause + 6;
		else rest_of_line += strlen (rest_of_line);
	}
}

