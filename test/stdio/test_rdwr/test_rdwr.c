/* Copyright (C) 1991, 1992 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* GROK - fseek was added so that it would work with BSD's stdio.  It is */
/*        not necessary for glibc, but that seems to be an artifact...   */


int main (int argc, char **argv)
{
  static const char hello[] = "Hello, world.\n";
  static const char replace[] = "Hewwo, world.\n";
  static const size_t replace_from = 2, replace_to = 4;
  char filename[FILENAME_MAX];
  char *name = strrchr(*argv, '/');
  char buf[BUFSIZ];
  FILE *f;
  int lose = 0;

  if (name != NULL)
    ++name;
  else
    name = *argv;

  (void) sprintf(filename, "%s.test", name);

  f = fopen(filename, "w+");
  if (f == NULL)
    {
      perror(filename);
      exit(1);
    }

  (void) fputs(hello, f);
  rewind(f);
  (void) fgets(buf, sizeof(buf), f);
  rewind(f);
  (void) fputs(buf, f);
  rewind(f);
  {
    register size_t i;
    for (i = 0; i < replace_from; ++i)
      {
	int c = getc(f);
	if (c == EOF)
	  {
	    printf("EOF at %u.\n", i);
	    lose = 1;
	    break;
	  }
	else if (c != hello[i])
	  {
	    printf("Got '%c' instead of '%c' at %u.\n",
		   (unsigned char) c, hello[i], i);
	    lose = 1;
	    break;
	  }
      }
  }

  {
    long int where = ftell(f);
    fseek (f, where, SEEK_SET);
    if (where == replace_from)
      {
	register size_t i;
	for (i = replace_from; i < replace_to; ++i)
	  if (putc(replace[i], f) == EOF)
	    {
	      printf("putc('%c') got %s at %u.\n",
		     replace[i], strerror(errno), i);
	      lose = 1;
	      break;
	    }
      }
    else if (where == -1L)
      {
	printf("ftell got %s (should be at %u).\n",
	       strerror(errno), replace_from);
	lose = 1;
      }
    else
      {
	printf("ftell returns %u; should be %u.\n", (uint)where, replace_from);
	lose = 1;
      }
  }

  if (!lose)
    {
      rewind(f);
      if (fgets(buf, sizeof(buf), f) == NULL)
	{
	  printf("fgets got %s.\n", strerror(errno));
	  lose = 1;
	}
      else if (strcmp(buf, replace))
	{
	  printf("Read \"%s\" instead of \"%s\".\n", buf, replace);
	  lose = 1;
	}
    }

  if (lose)
    printf("Test FAILED!  Losing file is \"%s\".\n", filename);
  else
    {
      (void) remove(filename);
      puts("Test succeeded.");
    }

  if (lose) {
     printf ("Test failed!!!\n");
  }
  exit(0);
}
