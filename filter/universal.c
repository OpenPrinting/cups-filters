/*
 * Include necessary headers...
 */

#include <cupsfilters/filter.h>
#include <config.h>
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
  int           ret;
  char          *p;
  cf_filter_universal_parameter_t universal_parameters;
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

  if ((p = getenv("CONTENT_TYPE")) != NULL)
    universal_parameters.input_format = strdup(p);
  else
    universal_parameters.input_format = NULL;

  if ((p = getenv("FINAL_CONTENT_TYPE")) != NULL)
    universal_parameters.output_format = strdup(p);
  else
    universal_parameters.output_format = NULL;

  if ((p = getenv("CUPS_DATADIR")) != NULL)
    universal_parameters.texttopdf_params.data_dir = strdup(p);
  else
    universal_parameters.texttopdf_params.data_dir = strdup(CUPS_DATADIR);

  if ((p = getenv("CHARSET")) != NULL)
    universal_parameters.texttopdf_params.char_set = strdup(p);
  else
    universal_parameters.texttopdf_params.char_set = NULL;

  universal_parameters.texttopdf_params.content_type =
    universal_parameters.input_format;

  if ((p = getenv("CLASSIFICATION")) != NULL)
    universal_parameters.texttopdf_params.classification = strdup(p);
  else
    universal_parameters.texttopdf_params.classification = NULL;

  ret = cfFilterCUPSWrapper(argc, argv, cfFilterUniversal, &universal_parameters,
			  &JobCanceled);

  if (ret)
    fprintf(stderr, "ERROR: universal filter failed.\n");

  free(universal_parameters.input_format);
  free(universal_parameters.output_format);
  free(universal_parameters.texttopdf_params.data_dir);
  free(universal_parameters.texttopdf_params.char_set);
  free(universal_parameters.texttopdf_params.classification);

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

