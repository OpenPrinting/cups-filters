//
// Legacy CUPS filter wrapper for cfFilterGhostscript() for cups-filters.
//
// PostScript to PDF filter, also produces searchable PDF (OCR) for scans.
//
// Copyright © 2020-2022 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif // HAVE_CONFIG_H

#include <cupsfilters/filter.h>
#include <ppd/ppd-filter.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>


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
  // Fire up the cfFilterGhostscript() filter function.
  //

  cf_filter_out_format_t outformat = CF_FILTER_OUT_FORMAT_PDF;
  void *parameters = &outformat;
#ifdef HAVE_OCR_OUTPUT
  const char *final_type = getenv("FINAL_CONTENT_TYPE");

  // Searchable PDF (OCR): Let cfFilterGhostscript() select the output
  // by the job's final type. On PostScript input (first step of a
  // PostScript -> pdftopdf -> OCR chain) it only converts to PDF, the
  // OCR is done by the later run on the PDF.
  if (final_type && strstr(final_type, "vnd.cups-pdf-ocr"))
    parameters = NULL;
#endif // HAVE_OCR_OUTPUT
  ret = ppdFilterCUPSWrapper(argc, argv, cfFilterGhostscript, parameters,
			     &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: gstopdf filter failed.\n");

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
