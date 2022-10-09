/*
 *   Advanced PCL command filter for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 *
 * Contents:
 *
 *   main() - Main entry and command processing.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups.h>
#include <ppd/ppd.h>
#include <cupsfilters/driver.h>
#include <cupsfilters/image.h>
#include "pcl.h"
#include <string.h>
#include <ctype.h>


/*
 * 'main()' - Main entry and processing of driver.
 */

int						/* O - Exit status */
main(int  argc,					/* I - Number of command-line arguments */
     char *argv[])				/* I - Command-line arguments */
{
  FILE		*fp;				/* Command file */
  char		line[1024],			/* Line from file */
		*lineptr;			/* Pointer into line */
  int		feedpage;			/* Feed the page */
  ppd_file_t	*ppd;				/* PPD file */


 /*
  * Check for valid arguments...
  */

  if (argc < 6 || argc > 7)
  {
   /*
    * We don't have the correct number of arguments; write an error message
    * and return.
    */

    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
	    argv[0]);
    return (1);
  }

 /*
  * Open the PPD file...
  */

  if ((ppd = ppdOpenFile(getenv("PPD"))) == NULL)
  {
    fputs("ERROR: Unable to open PPD file!\n", stderr);
    return (1);
  }

 /*
  * Open the command file as needed...
  */

  if (argc == 7)
  {
    if ((fp = fopen(argv[6], "r")) == NULL)
    {
      perror("ERROR: Unable to open command file - ");
      return (1);
    }
  }
  else
    fp = stdin;

 /*
  * Reset the printer...
  */

  cfWritePrintData("\033E", 2);

 /*
  * Read the commands from the file and send the appropriate commands...
  */

  feedpage = 0;

  while (fgets(line, sizeof(line), fp) != NULL)
  {
   /*
    * Drop trailing newline...
    */

    lineptr = line + strlen(line) - 1;
    if (*lineptr == '\n')
      *lineptr = '\0';

   /*
    * Skip leading whitespace...
    */

    for (lineptr = line; isspace(*lineptr); lineptr ++);

   /*
    * Skip comments and blank lines...
    */

    if (*lineptr == '#' || !*lineptr)
      continue;

   /*
    * Parse the command...
    */

    if (strncasecmp(lineptr, "Clean", 5) == 0 &&
        (ppd->model_number & PCL_INKJET))
    {
     /*
      * Clean heads...
      */

      cfWritePrintData("\033&b16WPML \004\000\006\001\004\001\005\001"
                         "\001\004\001\144", 22);
    }
    else
      fprintf(stderr, "ERROR: Invalid printer command \"%s\"!\n", lineptr);
  }

 /*
  * Eject the page as needed...
  */

  if (feedpage)
  {
    fputs("PAGE: 1 1\n", stderr);

    putchar(12);
  }

 /*
  * Reset the printer...
  */

  cfWritePrintData("\033E", 2);

 /*
  * Close the command file and return...
  */

  ppdClose(ppd);

  if (fp != stdin)
    fclose(fp);

  return (0);
}

