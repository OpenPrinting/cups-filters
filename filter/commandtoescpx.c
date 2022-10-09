/*
 *   Advanced EPSON ESC/P command filter for CUPS.
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
#include "escp.h"
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
  * Some EPSON printers need an additional command issued at the
  * beginning of each job to exit from USB "packet" mode...
  */

  if (ppd->model_number & ESCP_USB)
    cfWritePrintData("\000\000\000\033\001@EJL 1284.4\n@EJL     \n\033@", 29);

 /*
  * Reset the printer...
  */

  cfWritePrintData("\033@", 2);

 /*
  * Enter remote mode...
  */

  cfWritePrintData("\033(R\010\000\000REMOTE1", 13);
  feedpage = 0;

 /*
  * Read the commands from the file and send the appropriate commands...
  */

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

    if (strncasecmp(lineptr, "Clean", 5) == 0)
    {
     /*
      * Clean heads...
      */

      cfWritePrintData("CH\002\000\000\000", 6);
    }
    else if (strncasecmp(lineptr, "PrintAlignmentPage", 18) == 0)
    {
     /*
      * Print alignment page...
      */

      int phase;

      phase = atoi(lineptr + 18);

      cfWritePrintData("DT\003\000\000", 5);
      putchar(phase & 255);
      putchar(phase >> 8);
      feedpage = 1;
    }
    else if (strncasecmp(lineptr, "PrintSelfTestPage", 17) == 0)
    {
     /*
      * Print version info and nozzle check...
      */

      cfWritePrintData("VI\002\000\000\000", 6);
      cfWritePrintData("NC\002\000\000\000", 6);
      feedpage = 1;
    }
    else if (strncasecmp(lineptr, "ReportLevels", 12) == 0)
    {
     /*
      * Report ink levels...
      */

      cfWritePrintData("IQ\001\000\001", 5);
    }
    else if (strncasecmp(lineptr, "SetAlignment", 12) == 0)
    {
     /*
      * Set head alignment...
      */

      int phase, x;

      if (sscanf(lineptr + 12, "%d%d", &phase, &x) != 2)
      {
        fprintf(stderr, "ERROR: Invalid printer command \"%s\"!\n", lineptr);
        continue;
      }

      cfWritePrintData("DA\004\000", 4);
      putchar(0);
      putchar(phase);
      putchar(0);
      putchar(x);
      cfWritePrintData("SV\000\000", 4);
    }
    else
      fprintf(stderr, "ERROR: Invalid printer command \"%s\"!\n", lineptr);
  }

 /*
  * Exit remote mode...
  */

  cfWritePrintData("\033\000\000\000", 4);

 /*
  * Eject the page as needed...
  */

  if (feedpage)
  {
    fputs("PAGE: 1 1\n", stderr);

    putchar(13);
    putchar(10);
    putchar(12);
  }

 /*
  * Reset the printer...
  */

  cfWritePrintData("\033@", 2);

 /*
  * Close the command file and return...
  */

  ppdClose(ppd);

  if (fp != stdin)
    fclose(fp);

  return (0);
}

