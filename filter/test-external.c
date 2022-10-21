//
// Legacy CUPS filter wrapper for cfFilterExternal() and
// ppdFilterExternalCUPS() for cups-filters.
//
// Primarily for testing and debugging. CUPS filters which can be called
// by these filter functions can also be called directly instead of this
// wrapper.
//
// Copyright © 2020-2022 by OpenPrinting.
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
#include <config.h>


//
// Local globals...
//

static int		JobCanceled = 0; // Set to 1 on SIGTERM


//
// Local functions...
//

static void		cancel_job(int sig);


//
// 'main()' - Main entry.
//

int		   // O - Exit status
main(int  argc,	   // I - Number of command-line arguments
     char *argv[]) // I - Command-line arguments
{
  int           ret = 1;
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
  // Fire up the cfFilterExternal()/ppdFilterExternalCUPS() filter function
  //

  cf_filter_external_t parameters;
  char *val;

  parameters.num_options = 0;
  parameters.options = NULL;
  parameters.envp = NULL;
  if ((val = getenv("INTERFACE")) != NULL)
  {
    parameters.filter = val;
    parameters.exec_mode = -1;
    ret = ppdFilterCUPSWrapper(argc, argv, cfFilterExternal, &parameters,
			       &JobCanceled);
  }
  else if ((val = getenv("FILTER")) != NULL)
  {
    parameters.filter = val;
    parameters.exec_mode = 0;
    ret = ppdFilterCUPSWrapper(argc, argv, cfFilterExternal, &parameters,
			       &JobCanceled);
  }
  else if ((val = getenv("CUPSFILTER")) != NULL)
  {
    parameters.filter = val;
    parameters.exec_mode = 0;
    ret = ppdFilterCUPSWrapper(argc, argv, ppdFilterExternalCUPS, &parameters,
			       &JobCanceled);
  }
  else if ((val = getenv("CUPSBACKEND")) != NULL)
  {
    parameters.filter = val;
    if (argc < 6)
      parameters.exec_mode = 2;
    else
      parameters.exec_mode = 1;
    ret = ppdFilterCUPSWrapper(argc, argv, ppdFilterExternalCUPS, &parameters,
			       &JobCanceled);
  }
  else
    fprintf(stderr, "ERROR: No filter executable specified. Specify with one of INTERFACE, FILTER, CUPSFILTER, or CUPSBACKEND environment variables.\n");    

  if (ret)
    fprintf(stderr, "ERROR: cfFilterExternal()/ppdFilterExternalCUPS() filter function failed.\n");

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
