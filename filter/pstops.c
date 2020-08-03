/*
 * PostScript filter for CUPS (based on pstops() filter function).
 *
 * Copyright © 2020 by Till Kamppeter
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1993-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <errno.h>
#include <string.h>
#include <signal.h>


/*
 * Local globals...
 */

static int		JobCanceled = 0;/* Set to 1 on SIGTERM */


/*
 * Local functions...
 */

static void		cancel_job(int sig);


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int	        inputfd;		/* Print file descriptor*/
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  filter_data_t pstops_filter_data;
  int           ret;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]",
	    argv[0]);
    return (1);
  }

 /*
  * Register a signal handler to cleanly cancel a job.
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif /* HAVE_SIGSET */

 /*
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
    inputfd = 0; /* stdin */
  else
  {
   /*
    * Try to open the print file...
    */

    if ((inputfd = open(argv[6], O_RDONLY)) < 0)
    {
      if (!JobCanceled)
      {
        fprintf(stderr, "DEBUG: Unable to open \"%s\": %s\n", argv[6],
		strerror(errno));
	fprintf(stderr, "ERROR: Unable to open print file");
      }

      return (1);
    }
  }

 /*
  * Process command-line options...
  */

  options     = NULL;
  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Create data record to call the pstops filter function
  */

  pstops_filter_data.job_id = atoi(argv[1]);
  pstops_filter_data.job_user = argv[2];
  pstops_filter_data.job_title = argv[3];
  pstops_filter_data.copies = atoi(argv[4]);
  pstops_filter_data.job_attrs = NULL;      /* We use command line options */
  pstops_filter_data.printer_attrs = NULL;  /* We use the queue's PPD file */
  pstops_filter_data.num_options = num_options;
  pstops_filter_data.options = options; /* Command line options from 5th arg */
  pstops_filter_data.ppdfile = NULL;
  pstops_filter_data.ppd = NULL;  /* Filter function will load PPD according
				     to "PPD" environment variable. */
  pstops_filter_data.logfunc = cups_logfunc; /* Logging scheme of CUPS */
  pstops_filter_data.logdata = NULL;

 /*
  * Fire up the pstops() filter function (output to stdout, file descriptor 1
  */

  ret = pstops(inputfd, 1, 0, &JobCanceled, &pstops_filter_data);

  if (ret)
    fprintf(stderr, "ERROR: pstops filter function failed.\n");

  return (ret);
}


/*
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig)			/* I - Signal number (unused) */
{
  (void)sig;

  JobCanceled = 1;
}

