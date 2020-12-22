/*
 * Filter functions support for cups-filters.
 *
 * Copyright © 2020 by Till Kamppeter.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "filter.h"
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <cups/file.h>
#include <cups/array.h>
#include <ppd/ppd.h>


/*
 * Type definitions
 */

typedef struct filter_function_pid_s    /* Filter in filter chain */
{
  char          *name;                  /* Filter executable name */
  int           pid;                    /* PID of filter process */
} filter_function_pid_t;


/*
 * 'cups_logfunc()' - Output log messages on stderr, compatible to CUPS,
 *                    meaning that the debug level is represented by a
 *                    prefix like "DEBUG: ", "INFO: ", ...
 */

void
cups_logfunc(void *data,
	     filter_loglevel_t level,
	     const char *message,
	     ...)
{
  va_list arglist;


  (void)data; /* No extra data needed */

  switch(level)
  {
    case FILTER_LOGLEVEL_UNSPEC:
    case FILTER_LOGLEVEL_DEBUG:
    default:
      fprintf(stderr, "DEBUG: ");
      break;
    case FILTER_LOGLEVEL_INFO:
      fprintf(stderr, "INFO: ");
      break;
    case FILTER_LOGLEVEL_WARN:
      fprintf(stderr, "WARN: ");
      break;
    case FILTER_LOGLEVEL_ERROR:
    case FILTER_LOGLEVEL_FATAL:
      fprintf(stderr, "ERROR: ");
      break;
    case FILTER_LOGLEVEL_CONTROL:
      break;
  }      
  va_start(arglist, message);
  vfprintf(stderr, message, arglist);
  fflush(stderr);
  va_end(arglist);
  fputc('\n', stderr);
}


/*
 * 'cups_iscanceledfunc()' - Return 1 if the job is canceled, which is
 *                           the case when the integer pointed at by data
 *                           is not zero.
 */

int
cups_iscanceledfunc(void *data)
{
  return (*((int *)data) != 0 ? 1 : 0);
}


/*
 * 'filterCUPSWrapper()' - Wrapper function to use a filter function as
 *                         classic CUPS filter
 */

int					/* O - Exit status */
filterCUPSWrapper(
     int  argc,				/* I - Number of command-line args */
     char *argv[],			/* I - Command-line arguments */
     filter_function_t filter,          /* I - Filter function */
     void *parameters,                  /* I - Filter function parameters */
     int *JobCanceled)                  /* I - Var set to 1 when job canceled */
{
  int	        inputfd;		/* Print file descriptor*/
  int           inputseekable;          /* Is the input seekable (actual file
					   not stdin)? */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  filter_data_t filter_data;
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
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
  {
    inputfd = 0; /* stdin */
    inputseekable = 0;
  }
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

    inputseekable = 1;
  }

 /*
  * Process command-line options...
  */

  options     = NULL;
  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Create data record to call filter function and load PPD file
  */

  filter_data.job_id = atoi(argv[1]);
  filter_data.job_user = argv[2];
  filter_data.job_title = argv[3];
  filter_data.copies = atoi(argv[4]);
  filter_data.job_attrs = NULL;        /* We use command line options */
  filter_data.printer_attrs = NULL;    /* We use the queue's PPD file */
  filter_data.num_options = num_options;
  filter_data.options = options;       /* Command line options from 5th arg */
  filter_data.ppdfile = getenv("PPD"); /* PPD file name in the "PPD"
					  environment variable. */
  filter_data.ppd = filter_data.ppdfile ?
                    ppdOpenFile(filter_data.ppdfile) : NULL;
                                       /* Load PPD file */
  filter_data.logfunc = cups_logfunc;  /* Logging scheme of CUPS */
  filter_data.logdata = NULL;
  filter_data.iscanceledfunc = cups_iscanceledfunc; /* Job-is-canceled
						       function */
  filter_data.iscanceleddata = JobCanceled;

 /*
  * Prepare PPD file
  */

  ppdMarkDefaults(filter_data.ppd);
  ppdMarkOptions(filter_data.ppd, filter_data.num_options, filter_data.options);

 /*
  * Fire up the filter function (output to stdout, file descriptor 1)
  */

  return filter(inputfd, 1, inputseekable, &filter_data, parameters);
}


/*
 * 'filterPOpen()' - Pipe a stream to or from a filter function
 *                   Can be the input to or the output from the
 *                   filter function.
 */

int                              /* O - File decriptor */
filterPOpen(filter_function_t filter_func, /* I - Filter function */
	    int inputfd,         /* I - File descriptor input stream or -1 */
	    int outputfd,        /* I - File descriptor output stream or -1 */
	    int inputseekable,   /* I - Is input stream seekable? */
	    filter_data_t *data, /* I - Job and printer data */
	    void *parameters,    /* I - Filter-specific parameters */
	    int *filter_pid)     /* O - PID of forked filter process */
{
  int		pipefds[2],          /* Pipes for filters */
		pid,		     /* Process ID of filter */
                ret,
                infd, outfd;         /* Temporary file descriptors */
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * Check file descriptors...
  */

  if (inputfd < 0 && outputfd < 0)
  {
    if (log)
      log(ld, FILTER_LOGLEVEL_ERROR,
	  "filterPOpen: Either inputfd or outputfd must be < 0, not both");
    return (-1);
  }

  if (inputfd > 0 && outputfd > 0)
  {
    if (log)
      log(ld, FILTER_LOGLEVEL_ERROR,
	  "filterPOpen: One of inputfd or outputfd must be < 0");
    return (-1);
  }

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Open a pipe ...
  */

  if (pipe(pipefds) < 0) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterPOpen: Could not create pipe for %s: %s",
		 inputfd < 0 ? "input" : "output",
		 strerror(errno));
    return (-1);
  }

  if ((pid = fork()) == 0) {
   /*
    * Child process goes here...
    *
    * Update input and output FDs as needed...
    */

    if (inputfd < 0) {
      inputseekable = 0;
      infd = pipefds[0];
      outfd = outputfd;
      close(pipefds[1]);
    } else {
      infd = inputfd;
      outfd = pipefds[1];
      close(pipefds[0]);
    }

   /*
    * Execute filter function...
    */

    ret = (filter_func)(infd, outfd, inputseekable, data, parameters);

   /*
    * Close file descriptor and terminate the sub-process...
    */

    close(infd);
    close(outfd);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "filterPOpen: Filter function completed with status %d.",
		 ret);
    exit(ret);

  } else if (pid > 0) {
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "filterPOpen: Filter function (PID %d) started.", pid);

   /*
    * Save PID for waiting for or terminating the sub-process
    */

    *filter_pid = pid;

    /*
     * Return file descriptor to stream to or from
     */

    if (inputfd < 0) {
      close(pipefds[0]);
      return (pipefds[1]);
    } else {
      close(pipefds[1]);
      return (pipefds[0]);
    }

  } else {

    /*
     * fork() error
     */

    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterPOpen: Could not fork to start filter function: %s",
		 strerror(errno));
    return (-1);
  }
}


/*
 * 'filterPClose()' - Close a piped stream created with
 *                    filterPOpen().
 */

int                              /* O - Error status */
filterPClose(int fd,             /* I - Pipe file descriptor */
	     int filter_pid,     /* I - PID of forked filter process */
	     filter_data_t *data)
{
  int		status,		 /* Exit status */
                retval;		 /* Return value */
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * close the stream...
  */

  close(fd);

 /*
  * Wait for the child process to exit...
  */

  retval = 0;

 retry_wait:
  if (waitpid (filter_pid, &status, 0) == -1)
  {
    if (errno == EINTR)
      goto retry_wait;
    if (log)
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "filterPClose: Filter function (PID %d) stopped with an error: %s!",
	  filter_pid, strerror(errno));
    goto out;
  }

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
               "filterPClose: Filter function (PID %d) exited with no errors.",
               filter_pid);

  /* How did the filter function terminate */
  if (WIFEXITED(status))
    /* Via exit() anywhere or return() in the main() function */
    retval = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    /* Via signal */
    retval = 256 * WTERMSIG(status);

 out:
  return(retval);
}


/*
 * 'compare_filter_pids()' - Compare two filter PIDs...
 */

static int					/* O - Result of comparison */
compare_filter_pids(filter_function_pid_t *a,	/* I - First filter */
		    filter_function_pid_t *b)	/* I - Second filter */
{
  return (a->pid - b->pid);
}


/*
 * 'filterChain()' - Call filter functions in a chain to do a data
 *                   format conversion which non of the individual
 *                   filter functions does
 */

int                              /* O - Error status */
filterChain(int inputfd,         /* I - File descriptor input stream */
	    int outputfd,        /* I - File descriptor output stream */
	    int inputseekable,   /* I - Is input stream seekable? */
	    filter_data_t *data, /* I - Job and printer data */
	    void *parameters)    /* I - Filter-specific parameters */
{
  cups_array_t  *filter_chain = (cups_array_t *)parameters;
  filter_filter_in_chain_t *filter,  /* Current filter */
		*next;		     /* Next filter */
  int		current,	     /* Current filter */
		filterfds[2][2],     /* Pipes for filters */
		pid,		     /* Process ID of filter */
		status,		     /* Exit status */
		retval,		     /* Return value */
		ret;
  int		infd, outfd;         /* Temporary file descriptors */
  cups_array_t	*pids;		     /* Executed filters array */
  filter_function_pid_t	*pid_entry,  /* Entry in executed filters array */
		key;		     /* Search key for filters */
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Remove NULL filters...
  */

  for (filter = (filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter;
       filter = (filter_filter_in_chain_t *)cupsArrayNext(filter_chain)) {
    if (!filter->function) {
      if (log) log(ld, FILTER_LOGLEVEL_INFO,
		   "filterChain: Invalid filter: %s - Removing...",
		   filter->name ? filter->name : "Unspecified");
      cupsArrayRemove(filter_chain, filter);
    } else
      if (log) log(ld, FILTER_LOGLEVEL_INFO,
		   "filterChain: Running filter: %s",
		   filter->name ? filter->name : "Unspecified");
  }

 /*
  * Execute all of the filters...
  */

  pids            = cupsArrayNew((cups_array_func_t)compare_filter_pids, NULL);
  current         = 0;
  filterfds[0][0] = inputfd;
  filterfds[0][1] = -1;
  filterfds[1][0] = -1;
  filterfds[1][1] = -1;

  for (filter = (filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter;
       filter = next, current = 1 - current) {
    next = (filter_filter_in_chain_t *)cupsArrayNext(filter_chain);

    if (filterfds[1 - current][1] > 1) {
      close(filterfds[1 - current][0]);
      close(filterfds[1 - current][1]);
      filterfds[1 - current][0] = -1;
      filterfds[1 - current][1] = -1;
    }

    if (next) {
      if (pipe(filterfds[1 - current]) < 0) {
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "filterChain: Could not create pipe for output of %s: %s",
		     filter->name ? filter->name : "Unspecified filter",
		     strerror(errno));
	return (1);
      }
    } else
      filterfds[1 - current][1] = outputfd;

    if ((pid = fork()) == 0) {
     /*
      * Child process goes here...
      *
      * Update input and output FDs as needed...
      */

      infd = filterfds[current][0];
      outfd = filterfds[1 - current][1];
      close(filterfds[current][1]);
      close(filterfds[1 - current][0]);

      if (infd < 0)
	infd = open("/dev/null", O_RDONLY);

      if (outfd < 0)
	outfd = open("/dev/null", O_WRONLY);

     /*
      * Execute filter function...
      */

      ret = (filter->function)(infd, outfd, inputseekable, data,
			       filter->parameters);

      close(infd);
      close(outfd);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "filterChain: %s completed with status %d.",
		   filter->name ? filter->name : "Unspecified filter", ret);
      exit(ret);

    } else if (pid > 0) {
      if (log) log(ld, FILTER_LOGLEVEL_INFO,
		   "filterChain: %s (PID %d) started.",
		   filter->name ? filter->name : "Unspecified filter", pid);

      pid_entry = malloc(sizeof(filter_function_pid_t));
      pid_entry->pid = pid;
      pid_entry->name = filter->name ? filter->name : "Unspecified filter";
      cupsArrayAdd(pids, pid_entry);
    } else {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "filterChain: Could not fork to start %s: %s",
		   filter->name ? filter->name : "Unspecified filter",
		   strerror(errno));
      break;
    }

    inputseekable = 0;
  }

 /*
  * Close remaining pipes...
  */

  if (filterfds[0][1] > 1) {
    close(filterfds[0][0]);
    close(filterfds[0][1]);
  }

  if (filterfds[1][1] > 1) {
    close(filterfds[1][0]);
    close(filterfds[1][1]);
  }

 /*
  * Wait for the children to exit...
  */

  retval = 0;

  while (cupsArrayCount(pids) > 0) {
    if ((pid = wait(&status)) < 0) {
      if (errno == EINTR && iscanceled && iscanceled(icd)) {
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "filterChain: Job canceled, killing filters ...");
	for (pid_entry = (filter_function_pid_t *)cupsArrayFirst(pids);
	     pid_entry;
	     pid_entry = (filter_function_pid_t *)cupsArrayNext(pids)) {
	  kill(pid_entry->pid, SIGTERM);
	  free(pid_entry);
	}
	break;
      } else
	continue;
    }

    key.pid = pid;
    if ((pid_entry = (filter_function_pid_t *)cupsArrayFind(pids, &key)) !=
	NULL) {
      cupsArrayRemove(pids, pid_entry);

      if (status) {
	if (WIFEXITED(status)) {
	  if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		       "filterChain: %s (PID %d) stopped with status %d",
		       pid_entry->name, pid, WEXITSTATUS(status));
	} else {
	  if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		       "filterChain: %s (PID %d) crashed on signal %d",
		       pid_entry->name, pid, WTERMSIG(status));
	}
	retval = 1;
      } else {
	if (log) log(ld, FILTER_LOGLEVEL_INFO,
		       "filterChain: %s (PID %d) exited with no errors.",
		       pid_entry->name, pid);
      }

      free(pid_entry);
    }
  }

  cupsArrayDelete(pids);

  return (retval);
}


/*
 * 'filterSetCommonOptions()' - Set common filter options for media size, etc.
 *                              based on PPD file
 */

void
filterSetCommonOptions(
    ppd_file_t    *ppd,			/* I - PPD file */
    int           num_options,          /* I - Number of options */
    cups_option_t *options,             /* I - Options */
    int           change_size,		/* I - Change page size? */
    int           *Orientation,         /* I/O - Basic page parameters */
    int           *Duplex,
    int           *LanguageLevel,
    int           *ColorDevice,
    float         *PageLeft,
    float         *PageRight,
    float         *PageTop,
    float         *PageBottom,
    float         *PageWidth,
    float         *PageLength,
    filter_logfunc_t log,               /* I - Logging function,
					       NULL for no logging */
    void *ld)                           /* I - User data for logging function,
					       can be NULL */
{
  ppd_size_t	*pagesize;		/* Current page size */
  const char	*val;			/* Option value */


  *Orientation = 0;		/* 0 = portrait, 1 = landscape, etc. */
  *Duplex = 0;			/* Duplexed? */
  *LanguageLevel = 1;		/* Language level of printer */
  *ColorDevice = 1;		/* Color printer? */
  *PageLeft = 18.0f;		/* Left margin */
  *PageRight = 594.0f;		/* Right margin */
  *PageBottom = 36.0f;		/* Bottom margin */
  *PageTop = 756.0f;		/* Top margin */
  *PageWidth = 612.0f;		/* Total page width */
  *PageLength = 792.0f;		/* Total page length */

  if ((pagesize = ppdPageSize(ppd, NULL)) != NULL)
  {
    int corrected = 0;
    if (pagesize->width > 0) 
      *PageWidth = pagesize->width;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page width: %.0f",
		   pagesize->width);
      corrected = 1;
    }
    if (pagesize->length > 0) 
      *PageLength = pagesize->length;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page length: %.0f",
		   pagesize->length);
      corrected = 1;
    }
    if (pagesize->top >= 0 && pagesize->top <= *PageLength) 
      *PageTop = pagesize->top;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page top margin: %.0f",
		   pagesize->top);
      if (*PageLength >= *PageBottom)
	*PageTop = *PageLength - *PageBottom;
      else
	*PageTop = *PageLength;
      corrected = 1;
    }
    if (pagesize->bottom >= 0 && pagesize->bottom <= *PageLength) 
      *PageBottom = pagesize->bottom;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page bottom margin: %.0f",
		   pagesize->bottom);
      if (*PageLength <= *PageBottom)
	*PageBottom = 0.0f;
      corrected = 1;
    }
    if (*PageBottom == *PageTop)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f",
		   *PageBottom, *PageTop);
      *PageTop = *PageLength - *PageBottom;
      if (*PageBottom == *PageTop)
      {
	*PageBottom = 0.0f;
	*PageTop = *PageLength;
      }
      corrected = 1;
    }
    if (*PageBottom > *PageTop)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Bottom: %.0f; Top: %.0f",
		   *PageBottom, *PageTop);
      float swap = *PageBottom;
      *PageBottom = *PageTop;
      *PageTop = swap;
      corrected = 1;
    }

    if (pagesize->left >= 0 && pagesize->left <= *PageWidth) 
      *PageLeft = pagesize->left;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page left margin: %.0f",
		   pagesize->left);
      if (*PageWidth <= *PageLeft)
	*PageLeft = 0.0f;
      corrected = 1;
    }
    if (pagesize->right >= 0 && pagesize->right <= *PageWidth) 
      *PageRight = pagesize->right;
    else
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid value for page right margin: %.0f",
		   pagesize->right);
      if (*PageWidth >= *PageLeft)
	*PageRight = *PageWidth - *PageLeft;
      else
	*PageRight = *PageWidth;
      corrected = 1;
    }
    if (*PageLeft == *PageRight)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f",
		   *PageLeft, *PageRight);
      *PageRight = *PageWidth - *PageLeft;
      if (*PageLeft == *PageRight)
      {
	*PageLeft = 0.0f;
	*PageRight = *PageWidth;
      }
      corrected = 1;
    }
    if (*PageLeft > *PageRight)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f",
		   *PageLeft, *PageRight);
      float swap = *PageLeft;
      *PageLeft = *PageRight;
      *PageRight = swap;
      corrected = 1;
    }

    if (corrected)
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "PPD Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "Corrected Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   *PageWidth, *PageLength, *PageLeft,
		   *PageBottom, *PageRight, *PageTop);
    }
    else
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
  }

  if (ppd != NULL)
  {
    *ColorDevice   = ppd->color_device;
    *LanguageLevel = ppd->language_level;
  }

  if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
  {
    if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
        strcasecmp(val, "false") != 0)
    {
      if (ppd && ppd->landscape > 0)
        *Orientation = 1;
      else
        *Orientation = 3;
    }
  }
  else if ((val = cupsGetOption("orientation-requested",
				num_options, options)) != NULL)
  {
   /*
    * Map IPP orientation values to 0 to 3:
    *
    *   3 = 0 degrees   = 0
    *   4 = 90 degrees  = 1
    *   5 = -90 degrees = 3
    *   6 = 180 degrees = 2
    */

    *Orientation = atoi(val) - 3;
    if (*Orientation >= 2)
      *Orientation ^= 1;
  }

  if ((val = cupsGetOption("page-left", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageLeft = (float)atof(val);
	  break;
      case 1 :
          *PageBottom = (float)atof(val);
	  break;
      case 2 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 3 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-right", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 1 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 2 :
          *PageLeft = (float)atof(val);
	  break;
      case 3 :
          *PageBottom = (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-bottom", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageBottom = (float)atof(val);
	  break;
      case 1 :
          *PageLeft = (float)atof(val);
	  break;
      case 2 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 3 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
    }
  }

  if ((val = cupsGetOption("page-top", num_options, options)) != NULL)
  {
    switch (*Orientation & 3)
    {
      case 0 :
          *PageTop = *PageLength - (float)atof(val);
	  break;
      case 1 :
          *PageRight = *PageWidth - (float)atof(val);
	  break;
      case 2 :
          *PageBottom = (float)atof(val);
	  break;
      case 3 :
          *PageLeft = (float)atof(val);
	  break;
    }
  }

  if (change_size)
    filterUpdatePageVars(*Orientation, PageLeft, PageRight,
			 PageTop, PageBottom, PageWidth, PageLength);

  if (ppdIsMarked(ppd, "Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "Duplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "JCLDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "EFDuplexing", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "EFDuplexing", "DuplexTumble") ||
      ppdIsMarked(ppd, "ARDuplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "ARDuplex", "DuplexTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexNoTumble") ||
      ppdIsMarked(ppd, "KD03Duplex", "DuplexTumble"))
    *Duplex = 1;

  return;
}


/*
 * 'filterUpdatePageVars()' - Update the page variables for the orientation.
 */

void
filterUpdatePageVars(int Orientation,
		     float *PageLeft, float *PageRight,
		     float *PageTop, float *PageBottom,
		     float *PageWidth, float *PageLength)
{
  float		temp;			/* Swapping variable */


  switch (Orientation & 3)
  {
    case 0 : /* Portait */
        break;

    case 1 : /* Landscape */
	temp        = *PageLeft;
	*PageLeft   = *PageBottom;
	*PageBottom = temp;

	temp        = *PageRight;
	*PageRight  = *PageTop;
	*PageTop    = temp;

	temp        = *PageWidth;
	*PageWidth  = *PageLength;
	*PageLength = temp;
	break;

    case 2 : /* Reverse Portrait */
	temp        = *PageWidth - *PageLeft;
	*PageLeft   = *PageWidth - *PageRight;
	*PageRight  = temp;

	temp        = *PageLength - *PageBottom;
	*PageBottom = *PageLength - *PageTop;
	*PageTop    = temp;
        break;

    case 3 : /* Reverse Landscape */
	temp        = *PageWidth - *PageLeft;
	*PageLeft   = *PageWidth - *PageRight;
	*PageRight  = temp;

	temp        = *PageLength - *PageBottom;
	*PageBottom = *PageLength - *PageTop;
	*PageTop    = temp;

	temp        = *PageLeft;
	*PageLeft   = *PageBottom;
	*PageBottom = temp;

	temp        = *PageRight;
	*PageRight  = *PageTop;
	*PageTop    = temp;

	temp        = *PageWidth;
	*PageWidth  = *PageLength;
	*PageLength = temp;
	break;
  }
}
