/*
 *   pdftoippprinter
 *
 *   System-V-interface-style CUPS filter for PPD-less printing of PDF input
 *   data on IPP printers which advertise themselves via Bonjour/DNS-SD.
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
 */

/*
 * Include necessary headers...
 */

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
#include <config.h>
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
  const char	*argv_nt[8];		/* NULL-terminated array of the command
					   line arguments */
  cups_array_t  *filter_chain;          /* Filter chain to execute */
  int		exit_status = 0;	/* Exit status */
  char		*filter;
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

    fprintf(stderr, "DEBUG: pdftoippprinter - copying to temp print file \"%s\"\n",
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

  for (i = 0; i < 6; i++)
    argv_nt[i] = argv[i];
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
      if (filter_present("gstoraster"))
	cupsArrayAdd(filter_chain, "gstoraster");
      else
      {
	fprintf(stderr,
		"DEBUG: Filter gstoraster missing for \"output-format=%s\", using pdftoraster.\n", val);
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
      /*if (filter_present("rastertopwg"))
	cupsArrayAdd(filter_chain, "rastertopwg");
      else
      {
	fprintf(stderr,
		"ERROR: Filter rastertopwg missing for \"output-format=%s\"\n", val);
	exit_status = 1;
	goto error;
      }*/
    }
    else if (strcasestr(val, "pdf"))
      output_format = PDF;
    else if (strcasestr(val, "postscript"))
    {
      output_format = POSTSCRIPT;
      if (filter_present("pdftops"))
	cupsArrayAdd(filter_chain, "pdftops");
      else
      {
	fprintf(stderr,
		"ERROR: Filter pdftops missing for \"output-format=%s\"\n", val);
	exit_status = 1;
	goto error;
      }
    }
    else if (strcasestr(val, "pcl"))
    {
      if (strcasestr(val, "xl"))
      {
	output_format = PCLXL;
	if (filter_present("gstopxl"))
	  cupsArrayAdd(filter_chain, "gstopxl");
	else
	{
	  fprintf(stderr,
		  "DEBUG: Filter gstopxl missing for \"output-format=%s\", falling back to PCL 5c/e.\n", val);
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
    if (filter_present("gstoraster"))
      cupsArrayAdd(filter_chain, "gstoraster");
    else
    {
      fprintf(stderr,
	      "DEBUG: Filter gstoraster missing for \"output-format=%s\", using pdftoraster.\n", val);
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

    execv(filter, argv);

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
 * End
 */
