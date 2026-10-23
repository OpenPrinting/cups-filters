//
// Legacy CUPS filter wrapper for cfFilterGhostscriptOCR() for cups-filters.
//
// PDF/PostScript (scan) to searchable PDF filter.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include <cupsfilters/filter.h>
#include <ppd/ppd-filter.h>
#include <signal.h>


//
// Local globals...
//

static int JobCanceled = 0; // Set to 1 on SIGTERM

//
// Local functions...
//

static void		cancel_job(int sig);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  int           ret;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		// Actions for POSIX signals
#endif // HAVE_SIGACTION && !HAVE_SIGSET

  //
  // Register a signal handler to cleanly cancel a job.
  //

#ifdef HAVE_SIGSET // Use System V signals over POSIX to avoid bugs
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif // HAVE_SIGSET

  //
  // Fire up the cfFilterGhostscriptOCR() filter function. All settings
  // come from the job options (ocr-language, ocr-engine, ...).
  //

  ret = ppdFilterCUPSWrapper(argc, argv, cfFilterGhostscriptOCR, NULL,
			     &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: gstopdfocr filter failed.\n");

  return (ret);
}


//
// 'cancel_job()' - Flag the job as canceled.
//

static void
cancel_job(int sig)			// I - Signal number (unused)
{
  (void)sig;

  JobCanceled = 1;
}
