/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <signal.h>
#include <fontconfig/fontconfig.h>
#include <config.h>

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
  * Fire up the cfFilterTextToPDF() filter function
  */
 cf_filter_texttopdf_parameter_t parameters;
 char *p;

 if ((p = getenv("CUPS_DATADIR")) != NULL)
   parameters.data_dir = p;
 else
   parameters.data_dir = CUPS_DATADIR;
 if ((p = getenv("CHARSET")) != NULL)
   parameters.char_set = p;
 else
   parameters.char_set = NULL;
 if ((p = getenv("CONTENT_TYPE")) != NULL)
   parameters.content_type = p;
 else
   parameters.content_type = NULL;
 if ((p = getenv("CLASSIFICATION")) != NULL)
   parameters.classification = p;
 else
   parameters.classification = NULL;

  ret = cfFilterCUPSWrapper(argc, argv, cfFilterTextToPDF, &parameters, &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: texttopdf filter function failed.\n");

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
