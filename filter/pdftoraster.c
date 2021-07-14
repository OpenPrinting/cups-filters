/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
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
 * 'main()' - Main entry and processing of driver.
 */

int		   /* O - Exit status */
main(int  argc,	   /* I - Number of command-line arguments */
     char *argv[]) /* I - Command-line arguments */
{
  int           ret;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

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
  * Fire up the pdftoraster() filter function
  */

  filter_out_format_t outformat = OUTPUT_FORMAT_CUPS_RASTER;
  char *t = getenv("FINAL_CONTENT_TYPE");
  if (t) {
    if (strcasestr(t, "pwg"))
      outformat = OUTPUT_FORMAT_PWG_RASTER;
    else if (strcasestr(t, "cups"))
      outformat = OUTPUT_FORMAT_CUPS_RASTER;
  }

  ret = filterCUPSWrapper(argc, argv, pdftoraster, &outformat, &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: pdftoraster filter function failed.\n");

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