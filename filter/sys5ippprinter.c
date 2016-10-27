/*
 *   sys5ippprinter
 *
 *   System-V-interface-style CUPS filter for PPD-less printing of PDF and
 *   PWG Raster input data on IPP printers which advertise themselves via
 *   Bonjour/DNS-SD.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2006 by Easy Software Products.
 *   Copyright 2011-2013 by Till Kamppeter
 *
 * Contents:
 *
 *   main()           - Main entry for filter...
 *   cancel_job()     - Flag the job as canceled.
 *   filter_present() - Is the requested filter actually installed?
 *   compare_pids()   - Compare process IDs for sorting PID list
 *   exec_filter()    - Execute a filter process
 *   exec_filters()   - Execute a filter chain
 *   open_pipe()      - Create a pipe to transfer data from filter to filter
 *   get_option_in_str() - Get an option value from a string like argv[5]
 *   set_option_in_str() - Set an option value in a string like argv[5]
 */

/*
 * Include necessary headers...
 */

#include <config.h>
#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/file.h>
#include <signal.h>
#include <sys/wait.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <cupsfilters/image-private.h>

#define MAX_CHECK_COMMENT_LINES	20

/*
 * Type definitions
 */

typedef unsigned output_format_t;
enum output_format_e {PDF = 0, POSTSCRIPT = 1, PWGRASTER = 2, PCLXL = 3, PCL = 4};
typedef struct filter_pid_s             /* Filter in filter chain */
{
  char          *name;                  /* Filter executable name */
  int           pid;                    /* PID of filter process */
} filter_pid_t;

/*
 * Local functions...
 */

static void		cancel_job(int sig);
static int              filter_present(const char *filter);
static int		compare_pids(filter_pid_t *a, filter_pid_t *b);
static int		exec_filter(const char *filter, char **argv,
			            int infd, int outfd);
static int		exec_filters(cups_array_t *filters, char **argv);
static int		open_pipe(int *fds);
static char*		get_option_in_str(char *buf, const char *option,
					  int return_value);
static void		set_option_in_str(char *buf, int buflen,
					  const char *option,
					  const char *value);

/*
 * Local globals...
 */

static int		job_canceled = 0;


/*
 * 'main()' - Main entry for filter...
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  output_format_t    output_format;     /* Output format */
  int		fd = 0;			/* Copy file descriptor */
  char		*filename,		/* PDF file to convert */
		tempfile[1024];		/* Temporary file */
  char		buffer[8192];		/* Copy buffer */
  int		bytes;			/* Bytes copied */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  const char	*val;			/* Option value */
  char	        *argv_nt[8];		/* NULL-terminated array of the command
					   line arguments */
  int           optbuflen;
  cups_array_t  *filter_chain;          /* Filter chain to execute */
  int		exit_status = 0;	/* Exit status */
  int		color_printing;		/* Do we print in color? */
  char		*filter, *p;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */
  static const char * const color_mode_option_names[] =
    {	/* Possible names for a color mode option */
      "pwg-raster-document-type",
      "PwgRasterDocumentType",
      "print-color-mode",
      "PrintColorMode",
      "color-space",
      "ColorSpace",
      "color-model",
      "ColorModel",
      NULL
    };


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Make sure we have the right number of arguments for CUPS!
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job user title copies options [file]\n",
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
  * Copy stdin if needed...
  */

  if (argc == 6)
  {
   /*
    * Copy stdin to a temp file...
    */

    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      perror("DEBUG: Unable to copy PDF file");
      return (1);
    }

    fprintf(stderr, "DEBUG: sys5ippprinter - copying to temp print file \"%s\"\n",
            tempfile);

    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
      bytes = write(fd, buffer, bytes);

    close(fd);

    filename = tempfile;
  }
  else
  {
   /*
    * Use the filename on the command-line...
    */

    filename    = argv[6];
    tempfile[0] = '\0';
  }

 /*
  * Get the options from the fifth command line argument
  */

  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Copy the command line arguments into a NULL-terminated array
  */

  for (i = 0; i < 5; i++)
    argv_nt[i] = argv[i];
  /* We copy the contents of argv[5] into a somewhat larger buffer so that
     we can manipulate it */
  optbuflen = strlen(argv[5]) + 256;
  argv_nt[5] = calloc(optbuflen, sizeof(char));
  strcpy(argv_nt[5], (const char*)argv[5]);
  argv_nt[6] = filename;
  argv_nt[7] = NULL;

 /*
  * Create filter chain
  */

  filter_chain = cupsArrayNew(NULL, NULL);

 /*
  * Add the gziptoany filter if installed
  */

  if (filter_present("gziptoany"))
    cupsArrayAdd(filter_chain, "gziptoany");

 /*
  * If the rastertopdf filter is present and the input is in PWG Raster format
  * add the rastertopdf filter to the filter chain to support the PWG Raster
  * input. Same for JPEG input if imagetopdf is present. This way the PPD-less
  * auto-generated print queue emulates an IPP Everywhere printer, as PPDed
  * CUPS queues do.
  */
  
  if (filter_present("rastertopdf") && (val = getenv("CONTENT_TYPE")) != NULL &&
      strcasestr(val, "pwg-raster") != NULL) {
    cupsArrayAdd(filter_chain, "rastertopdf");
  } else if (filter_present("imagetopdf") &&
	     (val = getenv("CONTENT_TYPE")) != NULL &&
	     strcasestr(val, "jpeg") != NULL) {
    cupsArrayAdd(filter_chain, "imagetopdf");
  }

 /*
  * Check the presence of the pdftopdf filter and add it to the filter
  * chain if it is there
  */

  if (filter_present("pdftopdf"))
    cupsArrayAdd(filter_chain, "pdftopdf");

 /*
  * Select the output format: PDF, PostScript, PWG Raster, PCL-XL, and
  * PCL 5c/e
  * Add the needed filters to the filter chain
  */

  if ((val = cupsGetOption("output-format", num_options, options)) != NULL)
  {
    if (strcasestr(val, "raster"))
    {
      output_format = PWGRASTER;
      /* PWG Raster output */
      set_option_in_str(argv_nt[5], optbuflen, "MediaClass", NULL);
      set_option_in_str(argv_nt[5], optbuflen, "media-class", "PwgRaster");
      /* Page logging into page_log is not done by gstoraster/pdftoraster,
	 so let it be done by pdftopdf */
      set_option_in_str(argv_nt[5], optbuflen, "page-logging", "on");
      if (filter_present("gstoraster") && access(CUPS_GHOSTSCRIPT, X_OK) == 0)
	cupsArrayAdd(filter_chain, "gstoraster");
      else
      {
	fprintf(stderr,
		"DEBUG: Filter gstoraster or Ghostscript (%s) missing for \"output-format=%s\", using pdftoraster.\n", CUPS_GHOSTSCRIPT, val);
	if (filter_present("pdftoraster"))
	  cupsArrayAdd(filter_chain, "pdftoraster");
	else
	{
	  fprintf(stderr,
		  "ERROR: Filter pdftoraster missing for \"output-format=%s\"\n", val);
	  exit_status = 1;
	  goto error;
	}
      }
    }
    else if (strcasestr(val, "pdf"))
    {
      output_format = PDF;
      /* Page logging into page_log has to be done by pdftopdf */
      set_option_in_str(argv_nt[5], optbuflen, "page-logging", "on");
    }
    else if (strcasestr(val, "postscript"))
    {
      output_format = POSTSCRIPT;
      /* Page logging into page_log is done by pstops, so no need by
	 pdftopdf */
      set_option_in_str(argv_nt[5], optbuflen, "page-logging", "off");
      if (filter_present("pdftops"))
      {
	cupsArrayAdd(filter_chain, "pdftops");
	if (access(CUPS_GHOSTSCRIPT, X_OK) != 0)
	{
	  fprintf(stderr,
		  "DEBUG: Ghostscript (%s) missing for \"output-format=%s\", using Poppler's pdftops instead.\n", CUPS_GHOSTSCRIPT, val);
	  set_option_in_str(argv_nt[5], optbuflen, "pdftops-renderer",
			    "pdftops");
	}
	else if (access(CUPS_POPPLER_PDFTOPS, X_OK) != 0)
	{
	  fprintf(stderr,
		  "DEBUG: Poppler's pdftops (%s) missing for \"output-format=%s\", using Ghostscript instead.\n", CUPS_POPPLER_PDFTOPS, val);
	  set_option_in_str(argv_nt[5], optbuflen, "pdftops-renderer",
			    "gs");
	}
	else
	  set_option_in_str(argv_nt[5], optbuflen, "pdftops-renderer",
			    "hybrid");
      }
      else
      {
	fprintf(stderr,
		"ERROR: Filter pdftops missing for \"output-format=%s\"\n", val);
	exit_status = 1;
	goto error;
      }
    }
    else if ((p = strcasestr(val, "pcl")) != NULL)
    {
      if (!strcasecmp(p, "pclxl"))
      {
	output_format = PCLXL;
	if (filter_present("gstopxl") && access(CUPS_GHOSTSCRIPT, X_OK) == 0)
	{
	  cupsArrayAdd(filter_chain, "gstopxl");
	  /* Page logging into page_log is not done by gstopxl,
	     so let it be done by pdftopdf */
	  set_option_in_str(argv_nt[5], optbuflen, "page-logging", "on");
	}
	else
	{
	  fprintf(stderr,
		  "DEBUG: Filter gstopxl or Ghostscript (%s) missing for \"output-format=%s\", falling back to PCL 5c/e.\n", CUPS_GHOSTSCRIPT, val);
	  output_format = PCL;
	}
      }
      else
      {
	output_format = PCL;
      }
    }
    else
    {
      fprintf(stderr,
	      "ERROR: Invalid value for \"output-format\": \"%s\"\n", val);
      exit_status = 1;
      goto error;
    }
  }
  else
  {
    fprintf(stderr,
	    "ERROR: Missing option \"output-format\".\n");
    exit_status = 1;
    goto error;
  }
  if (output_format == PCL)
  {
    /* We need CUPS Raster as we want to use rastertopclx with unprintable
       margins */
    set_option_in_str(argv_nt[5], optbuflen, "MediaClass", NULL);
    set_option_in_str(argv_nt[5], optbuflen, "media-class", "");
    /* Page logging into page_log is done by rastertopclx, so no need by
       pdftopdf */
    set_option_in_str(argv_nt[5], optbuflen, "page-logging", "off");
    /* Does the client send info about margins? */
    if (!get_option_in_str(argv_nt[5], "media-left-margin", 0) &&
	!get_option_in_str(argv_nt[5], "media-right-margin", 0) &&
	!get_option_in_str(argv_nt[5], "media-top-margin", 0) &&
	!get_option_in_str(argv_nt[5], "media-bottom-margin", 0))
    {
      /* Set default 12pt margins if there is no info about printer's
	 unprintable margins (100th of mm units, 12.0 * 2540.0 / 72.0 = 423.33)
      */
      set_option_in_str(argv_nt[5], optbuflen, "media-left-margin", "423.33");
      set_option_in_str(argv_nt[5], optbuflen, "media-right-margin", "423.33");
      set_option_in_str(argv_nt[5], optbuflen, "media-top-margin", "423.33");
      set_option_in_str(argv_nt[5], optbuflen, "media-bottom-margin", "423.33");
    }
    /* Check whether the job is requested to be printed in color and if so,
       set the color space to RGB as this is the best color printing support
       in PCL 5c */
    color_printing = 0;
    for (i = 0; color_mode_option_names[i]; i ++)
    {
      p = get_option_in_str(argv_nt[5], color_mode_option_names[i], 1);
      if (p && (strcasestr(p, "RGB") || strcasestr(p, "CMY") ||
		strcasestr(p, "color")))
      {
	color_printing = 1;
	break;
      }
    }
    if (color_printing == 1)
    {
      /* Remove unneeded color mode options */
      for (i = 0; color_mode_option_names[i]; i ++)
	set_option_in_str(argv_nt[5], optbuflen, color_mode_option_names[i],
			  NULL);
      /* Set RGB as color mode */
      set_option_in_str(argv_nt[5], optbuflen, "print-color-mode", "RGB");
    }
    if (filter_present("gstoraster") && access(CUPS_GHOSTSCRIPT, X_OK) == 0)
      cupsArrayAdd(filter_chain, "gstoraster");
    else
    {
      fprintf(stderr,
	      "DEBUG: Filter gstoraster or Ghostscript (%s) missing for \"output-format=%s\", using pdftoraster.\n", CUPS_GHOSTSCRIPT, val);
      if (filter_present("pdftoraster"))
	cupsArrayAdd(filter_chain, "pdftoraster");
      else
      {
	fprintf(stderr,
		"ERROR: Filter pdftoraster missing for \"output-format=%s\"\n", val);
	exit_status = 1;
	goto error;
      }
    }
    if (filter_present("rastertopclx"))
      cupsArrayAdd(filter_chain, "rastertopclx");
    else
    {
      fprintf(stderr,
	      "ERROR: Filter rastertopclx missing for \"output-format=%s\"\n", val);
      exit_status = 1;
      goto error;
    }
  }

  fprintf(stderr,
	  "DEBUG: Printer supports output formats: %s\nDEBUG: Using following CUPS filter chain to convert input data to the %s format:",
	  val,
	  output_format == PDF ? "PDF" :
	  (output_format == POSTSCRIPT ? "Postscript" :
	   (output_format == PWGRASTER ? "PWG Raster" :
	    (output_format == PCLXL ? "PCL XL" :
	     (output_format == PCL ? "PCL 5c/e" : "unknown")))));
  for (filter = (char *)cupsArrayFirst(filter_chain);
       filter;
       filter = (char *)cupsArrayNext(filter_chain))
    fprintf(stderr, " %s", filter);
  fprintf(stderr, "\n");

 /*
  * Execute the filter chain
  */

  exit_status = exec_filters(filter_chain, (char **)argv_nt);

 /*
  * Cleanup and exit...
  */

  error:

  if (tempfile[0])
    unlink(tempfile);

  return (exit_status);
}


/*
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig)			/* I - Signal number (unused) */
{
  (void)sig;

  job_canceled = 1;
}


static int
filter_present(const char *filter)      /* I - Filter name */
{
  char		filter_path[1024];	/* Path to filter executable */
  const char	*cups_serverbin;	/* CUPS_SERVERBIN environment variable */

  if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
    cups_serverbin = CUPS_SERVERBIN;

  snprintf(filter_path, sizeof(filter_path), "%s/filter/%s",
           cups_serverbin, filter);

  if (access(filter_path, X_OK) == 0)
    return 1;

  return 0;
}


/*
 * 'compare_pids()' - Compare two filter PIDs...
 */

static int				/* O - Result of comparison */
compare_pids(filter_pid_t *a,		/* I - First filter */
             filter_pid_t *b)		/* I - Second filter */
{
  return (a->pid - b->pid);
}


/*
 * 'exec_filter()' - Execute a single filter.
 */

static int				/* O - Process ID or -1 on error */
exec_filter(const char *filter,		/* I - Filter to execute */
	    char       **argv,		/* I - Original command line args */
	    int        infd,		/* I - Stdin file descriptor */
	    int        outfd)		/* I - Stdout file descriptor */
{
  int		pid,			/* Process ID */
		fd;			/* Temporary file descriptor */

  if ((pid = fork()) == 0)
  {
   /*
    * Child process goes here...
    *
    * Update stdin/stdout/stderr as needed...
    */

    if (infd != 0)
    {
      if (infd < 0)
        infd = open("/dev/null", O_RDONLY);

      if (infd > 0)
      {
        dup2(infd, 0);
	close(infd);
      }
    }

    if (outfd != 1)
    {
      if (outfd < 0)
        outfd = open("/dev/null", O_WRONLY);

      if (outfd > 1)
      {
	dup2(outfd, 1);
	close(outfd);
      }
    }

    /* Send stderr to the Nirwana if we are running gziptoany, as
       gziptoany emits a false "PAGE: 1 1" */
    if (strcasestr(filter, "gziptoany")) {
      if ((fd = open("/dev/null", O_RDWR)) > 2)
      {
	dup2(fd, 2);
	close(fd);
      }
      fcntl(2, F_SETFL, O_NDELAY);
    }

    if ((fd = open("/dev/null", O_RDWR)) > 3)
    {
      dup2(fd, 3);
      close(fd);
    }
    fcntl(3, F_SETFL, O_NDELAY);

    if ((fd = open("/dev/null", O_RDWR)) > 4)
    {
      dup2(fd, 4);
      close(fd);
    }
    fcntl(4, F_SETFL, O_NDELAY);

   /*
    * Execute command...
    */

    execvp(filter, argv);

    perror(filter);

    exit(errno);
  }

  return (pid);
}


/*
 * 'exec_filters()' - Execute filters for the given file and options.
 */

static int				/* O - 0 on success, 1 on error */
exec_filters(cups_array_t  *filters,	/* I - Array of filters to run */
	     char	   **argv)	/* I - Filter options */
{
  int		i;			/* Looping var */
  char		program[1024];		/* Program to run */
  char		*filter,		/* Current filter */
		*next;			/* Next filter */
  int		current,		/* Current filter */
		filterfds[2][2],	/* Pipes for filters */
		pid,			/* Process ID of filter */
		status,			/* Exit status */
		retval;			/* Return value */
  cups_array_t	*pids;			/* Executed filters array */
  filter_pid_t	*pid_entry,		/* Entry in executed filters array */
		key;			/* Search key for filters */
  const char	*cups_serverbin;	/* CUPS_SERVERBIN environment variable */

 /*
  * Remove NULL ("-") filters...
  */

  for (filter = (char *)cupsArrayFirst(filters);
       filter;
       filter = (char *)cupsArrayNext(filters))
    if (!strcmp(filter, "-"))
      cupsArrayRemove(filters, filter);

  for (i = 0; argv[i]; i ++)
    fprintf(stderr, "DEBUG: argv[%d]=\"%s\"\n", i, argv[i]);

 /*
  * Execute all of the filters...
  */

  pids            = cupsArrayNew((cups_array_func_t)compare_pids, NULL);
  current         = 0;
  filterfds[0][0] = 0;
  filterfds[0][1] = -1;
  filterfds[1][0] = -1;
  filterfds[1][1] = -1;

  for (filter = (char *)cupsArrayFirst(filters);
       filter;
       filter = next, current = 1 - current)
  {
    next = (char *)cupsArrayNext(filters);

    if (filter[0] == '/')
      strncpy(program, filter, sizeof(program));
    else
    {
      if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
	cups_serverbin = CUPS_SERVERBIN;
      snprintf(program, sizeof(program), "%s/filter/%s", cups_serverbin,
	       filter);
    }

    if (filterfds[!current][1] > 1)
    {
      close(filterfds[1 - current][0]);
      close(filterfds[1 - current][1]);

      filterfds[1 - current][0] = -1;
      filterfds[1 - current][0] = -1;
    }

    if (next)
      open_pipe(filterfds[1 - current]);
    else
      filterfds[1 - current][1] = 1;

    pid = exec_filter(program, argv,
                      filterfds[current][0], filterfds[1 - current][1]);

    if (pid > 0)
    {
      fprintf(stderr, "INFO: %s (PID %d) started.\n", filter, pid);

      pid_entry = malloc(sizeof(filter_pid_t));
      pid_entry->pid = pid;
      pid_entry->name = filter;
      cupsArrayAdd(pids, pid_entry);
    }
    else
      break;

    argv[6] = NULL;
  }

 /*
  * Close remaining pipes...
  */

  if (filterfds[0][1] > 1)
  {
    close(filterfds[0][0]);
    close(filterfds[0][1]);
  }

  if (filterfds[1][1] > 1)
  {
    close(filterfds[1][0]);
    close(filterfds[1][1]);
  }

 /*
  * Wait for the children to exit...
  */

  retval = 0;

  while (cupsArrayCount(pids) > 0)
  {
    if ((pid = wait(&status)) < 0)
    {
      if (errno == EINTR && job_canceled)
      {
	fprintf(stderr, "DEBUG: Job canceled, killing filters ...\n");
	for (pid_entry = (filter_pid_t *)cupsArrayFirst(pids);
	     pid_entry;
	     pid_entry = (filter_pid_t *)cupsArrayNext(pids))
	  kill(pid_entry->pid, SIGTERM);
	job_canceled = 0;
      }
      else
	continue;
    }

    key.pid = pid;
    if ((pid_entry = (filter_pid_t *)cupsArrayFind(pids, &key)) != NULL)
    {
      cupsArrayRemove(pids, pid_entry);

      if (status)
      {
	if (WIFEXITED(status))
	  fprintf(stderr, "ERROR: %s (PID %d) stopped with status %d\n",
		  pid_entry->name, pid, WEXITSTATUS(status));
	else
	  fprintf(stderr, "ERROR: %s (PID %d) crashed on signal %d\n",
		  pid_entry->name, pid, WTERMSIG(status));

        retval = 1;
      }
      else
        fprintf(stderr, "INFO: %s (PID %d) exited with no errors.\n",
	        pid_entry->name, pid);

      free(pid_entry);
    }
  }

  cupsArrayDelete(pids);

  return (retval);
}


/*
 * 'open_pipe()' - Create a pipe which is closed on exec.
 */

static int				/* O - 0 on success, -1 on error */
open_pipe(int *fds)			/* O - Pipe file descriptors (2) */
{
 /*
  * Create the pipe...
  */

  if (pipe(fds))
  {
    fds[0] = -1;
    fds[1] = -1;

    return (-1);
  }

 /*
  * Set the "close on exec" flag on each end of the pipe...
  */

  if (fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);

    fds[0] = -1;
    fds[1] = -1;

    return (-1);
  }

  if (fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD) | FD_CLOEXEC))
  {
    close(fds[0]);
    close(fds[1]);

    fds[0] = -1;
    fds[1] = -1;

    return (-1);
  }

 /*
  * Return 0 indicating success...
  */

  return (0);
}


/*
 * Get option value in a string of options
 */

static char*				/* O - Value, NULL if option not set */
get_option_in_str(char *buf,		/* I - Buffer with option list string */
		  const char *option,	/* I - Option of which to get value */
		  int return_value)	/* I - Return value or only check
					   presence of option? */
{
  char *p1, *p2;
  char *result;

  if (!buf || !option)
    return NULL;
  if ((p1 = strcasestr(buf, option)) == NULL)
    return NULL;
  if (p1 > buf && *(p1 - 1) != ' ' && *(p1 - 1) != '\t')
    return NULL;
  p2 = p1 + strlen(option);
  if (*p2 == ' ' || *p2 == '\t' || *p2 == '\0')
    return "";
  if (*p2 != '=')
    return NULL;
  if (!return_value)
    return "";
  p1 = p2 + 1;
  for (p2 = p1; *p2 != ' ' && *p2 != '\t' && *p2 != '\0'; p2 ++);
  if (p2 == p1)
    return "";
  result = calloc(p2 - p1 + 1, sizeof(char));
  memcpy(result, p1, p2 - p1);
  result[p2 - p1] = '\0';
  return result;
}


/*
 * Set an option in a string of options
 */

void					/* O - 0 on success, 1 on error */
set_option_in_str(char *buf,		/* I - Buffer with option list string */
		  int buflen,		/* I - Length of buffer */
		  const char *option,	/* I - Option to change/add */
		  const char *value)	/* I - New value for option, NULL
					       removes option */
{
  char *p1, *p2;

  if (!buf || buflen == 0 || !option)
    return;
  /* Remove any occurrence of option in the string */
  p1 = buf;
  while (*p1 != '\0' && (p2 = strcasestr(p1, option)) != NULL)
  {
    if (p2 > buf && *(p2 - 1) != ' ' && *(p2 - 1) != '\t')
    {
      p1 = p2 + 1;
      continue;
    }
    p1 = p2 + strlen(option);
    if (*p1 != '=' && *p1 != ' ' && *p1 != '\t' && *p1 != '\0')
      continue;
    while (*p1 != ' ' && *p1 != '\t' && *p1 != '\0') p1 ++;
    while ((*p1 == ' ' || *p1 == '\t') && *p1 != '\0') p1 ++;
    memmove(p2, p1, strlen(buf) - (buf - p1) + 1);
    p1 = p2;
  }
  /* Add option=value to the end of the string */
  if (!value)
    return;
  p1 = buf + strlen(buf);
  *p1 = ' ';
  p1 ++;
  snprintf(p1, buflen - (buf - p1), "%s=%s", option, value);
  buf[buflen - 1] = '\0';
}

/*
 * End
 */
