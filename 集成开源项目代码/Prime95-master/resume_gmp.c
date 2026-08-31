/* Functions for reading/writing resume file lines.

Copyright 2001-2023 Paul Zimmermann, Alexander Kruppa and Cyril Bouvier.

This file is distributed under the MIT license.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#if !defined (_MSC_VER)
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

/* The checksum for savefile is the product of all mandatory fields, modulo
   the greatest prime below 2^32 */
#define CHKSUMMOD 4294967291U

/* Apple uses '\r' for newlines */
#define IS_NEWLINE(c) (((c) == '\n') || ((c) == '\r'))

/* Reads a string of characters from fd while they match the string s.
   Returns the number of matching characters that were read. 
*/

static int 
facceptstr (FILE *fd, const char *s)
{
  int c;
  unsigned i = 0;

  while (s[i] != 0 && (c = fgetc (fd)) != EOF)
    {
      if (c != s[i++])
        {
          ungetc (c, fd);
          return i-1;
        }
    }

  return i;
}

/* Accepts "\n" or "\r\n" or "\r". 
   Returns 1 if any of the three was read, 0 otherwise */

static int 
facceptnl (FILE *fd)
{
  int c, r = 0;

  c = fgetc (fd);
  if (c == '\r')
    {
      c = fgetc (fd);
      r = 1;
    }

  if (c == '\n')
    r = 1;
  else if (c != EOF)
    ungetc (c, fd);

  return r;
}

/* Reads a string from fd until the character "delim" or newline is seen, or 
   "len" characters have been written to "s" (including terminating null), 
   or EOF is reached. The "delim" and newline characters are left on the 
   stream.
   If s is NULL, characters are read from fd but not written anywhere.
   Returns the number of characters read.
*/

static int 
freadstrn (FILE *fd, char *s, char delim, unsigned int len)
{
  unsigned int i = 0;
  int c;

  while (i + 1 < len && (c = fgetc (fd)) != EOF)
    if (c == delim || IS_NEWLINE(c))
      {
        ungetc (c, fd);
        break;
      }
    else
      if (s != NULL)
        s[i++] = (char) c;

  if (i < len && s != NULL)
    s[i++] = 0;

  return i;
}

/* Alternative implementation of mpz_inp_str.  I'm getting weird behavior using GMP's version.  Perhaps because the GMP library built with MinGW */
/* use of FILE streams is incompatible with Windows library FILE streams. */

void my_mpz_inp_str (mpz_t x, FILE *fd, int base)
{
	char *buf = (char *) malloc (4000000);
	freadstrn (fd, buf, ';', 4000000);
	mpz_set_str (x, buf, 0);
	free (buf);
}

/* Reads an assignment from a save file. Return 1 if an assignment was successfully read, 0 if there are no more lines to read (at EOF) */

int read_resumefile_line (int thread_num, FILE *fd, mpz_t x, mpz_t n, mpz_t sigma, int *param, double *b1)
{
  int have_method, have_x, have_y, have_z, have_n, have_sigma, have_a, have_b1, have_checksum;
  unsigned int saved_checksum;
  char tag[16], buf[512];
  mpz_t A, y, z, x0, y0;

  while (!feof (fd))
    {
      /* Ignore empty lines */
      if (facceptnl (fd))
        continue;

      /* Ignore lines beginning with '#'*/
      if (facceptstr (fd, "#"))
        {
          while (!facceptnl (fd) && !feof (fd))
            fgetc (fd);
          continue;
        }

      if (feof (fd))
        break;

      have_method = have_x = have_y = have_z = have_n = have_sigma = have_a = have_b1 = have_checksum = 0;

      /* For compatibility reason, param = ECM_PARAM_SUYAMA by default */
      *param = 0;

      /* Set optional fields to zero */
      mpz_set_ui (sigma, 0);

      while (!facceptnl (fd) && !feof (fd))
        {
          freadstrn (fd, tag, '=', 16);

          if (!facceptstr (fd, "="))
            {
              sprintf (buf, "Resume warning, skipping line with no '=' after: %s\n", tag);
              OutputStr (thread_num, buf);
              goto error;
            }

          if (strcmp (tag, "METHOD") == 0)
            {
	      if (facceptstr (fd, "ECM") != 3)
	        {
		  OutputStr (thread_num, "Unsupported method\n");
		  goto error;
                }
	      have_method = 1;
            }
          else if (strcmp (tag, "X") == 0)
            {
              my_mpz_inp_str (x, fd, 0);
              have_x = 1;
            }
          else if (strcmp (tag, "Y") == 0)
            {
              mpz_init (y);
              my_mpz_inp_str (y, fd, 0);
              have_y = 1;
            }
          else if (strcmp (tag, "Z") == 0)
            {
              mpz_init (z);
              my_mpz_inp_str (z, fd, 0);
              have_z = 1;
            }
          else if (strcmp (tag, "X0") == 0)
            {
              mpz_init (x0);
              my_mpz_inp_str (x0, fd, 0);
              mpz_clear (x0);
            }
          else if (strcmp (tag, "Y0") == 0)
            {
              mpz_init (y0);
              my_mpz_inp_str (y0, fd, 0);
              mpz_clear (y0);
            }
          else if (strcmp (tag, "CHECKSUM") == 0)
            {
              if (fscanf (fd, "%u", &saved_checksum) != 1) goto error;
              have_checksum = 1;
            }
          else if (strcmp (tag, "COMMENT") == 0)
            {
              freadstrn (fd, NULL, ';', 255);
            }
          else if (strcmp (tag, "N") == 0)
	    {
	      freadstrn (fd, NULL, ';', 255);
            }
          else if (strcmp (tag, "SIGMA") == 0)
            {
              my_mpz_inp_str (sigma, fd, 0);
              have_sigma = 1;
            }
          else if (strcmp (tag, "PARAM") == 0)
            {
              if (fscanf (fd, "%d", param) != 1) goto error;
            }
          else if (strcmp (tag, "ETYPE") == 0)
	    {
	      int Etype;	  
              if (fscanf (fd, "%d", &Etype) != 1) goto error;
            }
          else if (strcmp (tag, "A") == 0)
	  {
	      mpz_init (A);	  
              my_mpz_inp_str (A, fd, 0);
              have_a = 1;
            }
          else if (strcmp (tag, "B1") == 0)
            {
              if (fscanf (fd, "%lf", b1) != 1) goto error;
              have_b1 = 1;
            }
          else if (strcmp (tag, "PROGRAM") == 0)
            {
              freadstrn (fd, NULL, ';', 255);
            }
          else if (strcmp (tag, "WHO") == 0)
            {
              freadstrn (fd, NULL, ';', 255);
            }
          else if (strcmp (tag, "TIME") == 0)
            {
              freadstrn (fd, NULL, ';', 255);
            }
          else /* Not a tag we know about */
            {
              sprintf (buf, "GMP save file line has unknown tag: %s\n", tag);
              OutputStr (thread_num, buf);
              goto error;
            }

          if (!facceptstr (fd, ";"))
            {
              sprintf (buf, "%s field not followed by semicolon\n", tag);
              OutputStr (thread_num, buf);
              goto error;
            }

          while (facceptstr (fd, " "));
        }

      /* Finished reading tags */

      if (!have_method || !have_x || !have_sigma || !have_b1)
        {
	  OutputStr (thread_num, "Save file line lacks fields\n");
          continue;
        }

#ifdef CHECKSUMS
      if (have_checksum)
        {
          mpz_t checksum;
          mpz_init (checksum);
          mpz_set_d (checksum, *b1);
          if (have_sigma) mpz_mul_ui (checksum, checksum, mpz_fdiv_ui (sigma, CHKSUMMOD));
          if (have_a) mpz_mul_ui (checksum, checksum, mpz_fdiv_ui (A, CHKSUMMOD));
          mpz_mul_ui (checksum, checksum, mpz_fdiv_ui (n, CHKSUMMOD));
          mpz_mul_ui (checksum, checksum, mpz_fdiv_ui (x, CHKSUMMOD));
          if (have_z) mpz_mul_ui (checksum, checksum, mpz_fdiv_ui (z, CHKSUMMOD));
          mpz_mul_ui (checksum, checksum, (*param+1)%CHKSUMMOD);
          if (mpz_fdiv_ui (checksum, CHKSUMMOD) != saved_checksum)
            {
	      sprintf (buf, "GMP Resume file line has bad checksum %u, expected %lu\n", saved_checksum, mpz_fdiv_ui (checksum, CHKSUMMOD));
	      OutputStr (thread_num, buf);
              mpz_clear (checksum);
              continue;
            }
          mpz_clear (checksum);
        }
#endif

      //mpz_mod (x, x, n);
      //if (have_y) mpz_mod(y, y, n);
      if (have_z)	/* Must normalize */
        {
          if (!mpz_invert (z, z, n)) /* Factor found? */
            {
              /* Oh great. What do we do with it now? */
              /* mpres_gcd (f, z, n); */
              sprintf (buf, "Oops, factor found while reading from save file.\n");
	      OutputStr (thread_num, buf);
            }
          mpz_mul (z, z, x);
          mpz_mod (x, z, n);
          mpz_clear (z);
        }

      if (have_a) mpz_clear(A);
      if (have_y) mpz_clear(y);
      return 1;

error:
      /* This can occur when reading Prime95 resume files,
         or files that have comment lines in them,
         or files that have a problem with the save line */
      /* In case of error, read rest of line and try next line */
      while (!facceptnl (fd) && !feof (fd))
        fgetc (fd);
    }

    /* We hit EOF without reading a proper save line */
    return 0;
}

/* This routine skips next resume file line */

void skip_resumefile_line (FILE *fd)
{
  char tag[16];
  while (!feof (fd)) {
    freadstrn (fd, tag, '=', 16);
    while (!facceptnl (fd) && !feof (fd)) fgetc (fd);	// Ignore rest of line
    if (strcmp (tag, "METHOD") == 0) break;		// Return after a line beginning with METHOD= is read
  }
}

/* This routine peeks at N= value on the current resume file line */

int peek_resumefile_line (FILE *fd, mpz_t n)
{
  char tag[16];
  fpos_t pos;
  fgetpos (fd, &pos);
  while (!feof (fd)) {
    freadstrn (fd, tag, '=', 16);
    if (strcmp (tag, "METHOD") != 0) {				// Ignore lines that do not begin with METHOD=
	while (!facceptnl (fd) && !feof (fd)) fgetc (fd);	// Ignore rest of line
	continue;
    }
    while (!feof (fd)) {
        freadstrn (fd, NULL, ';', 255);				// Skip to ';'
	facceptstr (fd, ";");					// Skip ';'
	while (facceptstr (fd, " ") || facceptnl (fd));		// Skip blanks
	freadstrn (fd, tag, '=', 16);				// Get next tag
        facceptstr (fd, "=");
	if (strcmp (tag, "N") == 0) {
		my_mpz_inp_str (n, fd, 0);
		fsetpos (fd, &pos);
		return 1;
	}
    }
  }
  /* We hit EOF without finding an N= line */
  return 0;
}


