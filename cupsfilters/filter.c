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

#include "config.h"
#include "filter.h"
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <cups/file.h>
#include <cups/array.h>
#include <ppd/ppd.h>

extern char **environ;

/*
 * Type definitions
 */

typedef struct filter_function_pid_s    /* Filter in filter chain */
{
  char          *name;                  /* Filter executable name */
  int           pid;                    /* PID of filter process */
} filter_function_pid_t;


/*
 * 'fcntl_add_cloexec()' - Add FD_CLOEXEC flag to the flags
 *                         of a given file descriptor.
 */

static int                /* Return value of fcntl() */
fcntl_add_cloexec(int fd) /* File descriptor to add FD_CLOEXEC to */
{
  return fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}


/*
 * 'fcntl_add_nodelay()' - Add O_NODELAY flag to the flags
 *                         of a given file descriptor.
 */

static int                 /* Return value of fcntl() */
fcntl_add_nonblock(int fd) /* File descriptor to add O_NONBLOCK to */
{
  return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}


/*
 * 'cfCUPSLogFunc()' - Output log messages on stderr, compatible to CUPS,
 *                    meaning that the debug level is represented by a
 *                    prefix like "DEBUG: ", "INFO: ", ...
 */

void
cfCUPSLogFunc(void *data,
	     cf_loglevel_t level,
	     const char *message,
	     ...)
{
  va_list arglist;


  (void)data; /* No extra data needed */

  switch(level)
  {
    case CF_LOGLEVEL_UNSPEC:
    case CF_LOGLEVEL_DEBUG:
    default:
      fprintf(stderr, "DEBUG: ");
      break;
    case CF_LOGLEVEL_INFO:
      fprintf(stderr, "INFO: ");
      break;
    case CF_LOGLEVEL_WARN:
      fprintf(stderr, "WARN: ");
      break;
    case CF_LOGLEVEL_ERROR:
    case CF_LOGLEVEL_FATAL:
      fprintf(stderr, "ERROR: ");
      break;
    case CF_LOGLEVEL_CONTROL:
      break;
  }      
  va_start(arglist, message);
  vfprintf(stderr, message, arglist);
  va_end(arglist);
  fputc('\n', stderr);
  fflush(stderr);
}


/*
 * 'cfCUPSIsCanceledFunc()' - Return 1 if the job is canceled, which is
 *                           the case when the integer pointed at by data
 *                           is not zero.
 */

int
cfCUPSIsCanceledFunc(void *data)
{
  return (*((int *)data) != 0 ? 1 : 0);
}


/*
 * 'cfFilterCUPSWrapper()' - Wrapper function to use a filter function as
 *                         classic CUPS filter
 */

int					/* O - Exit status */
cfFilterCUPSWrapper(
     int  argc,				/* I - Number of command-line args */
     char *argv[],			/* I - Command-line arguments */
     cf_filter_function_t filter,          /* I - Filter function */
     void *parameters,                  /* I - Filter function parameters */
     int *JobCanceled)                  /* I - Var set to 1 when job canceled */
{
  int	        inputfd;		/* Print file descriptor*/
  int           inputseekable;          /* Is the input seekable (actual file
					   not stdin)? */
  int		num_options;		/* Number of print options */
  cups_option_t	*options;		/* Print options */
  cf_filter_data_t filter_data;
  int           retval = 0;


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
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
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

  if ((filter_data.printer = getenv("PRINTER")) == NULL)
    filter_data.printer = argv[0];
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
  filter_data.back_pipe[0] = 3;        /* CUPS uses file descriptor 3 for */
  filter_data.back_pipe[1] = 3;        /* the back channel */
  filter_data.side_pipe[0] = 4;        /* CUPS uses file descriptor 4 for */
  filter_data.side_pipe[1] = 4;        /* the side channel */
  filter_data.logfunc = cfCUPSLogFunc;  /* Logging scheme of CUPS */
  filter_data.logdata = NULL;
  filter_data.iscanceledfunc = cfCUPSIsCanceledFunc; /* Job-is-canceled
						       function */
  filter_data.iscanceleddata = JobCanceled;

 /*
  * Load and prepare the PPD file
  */

  retval = cfFilterLoadPPD(&filter_data);

 /*
  * Fire up the filter function (output to stdout, file descriptor 1)
  */

  if (!retval)
    retval = filter(inputfd, 1, inputseekable, &filter_data, parameters);

 /*
  * Clean up
  */

  cupsFreeOptions(num_options, options);
  cfFilterFreePPD(&filter_data);

  return retval;
}


/*
 * 'cfFilterLoadPPD()' - When preparing the data structure for calling
 *                       one or more filter functions. Load the PPD
 *                       file specified by the file name in the
 *                       "ppdfile" field of the data structure. If the
 *                       file name is NULL do nothing. If the PPD got
 *                       successfully loaded also set up its cache,
 *                       and mark default settings and if supplied in
 *                       the data structure, also option settings.
 */

int					  /* O - Error status */
cfFilterLoadPPD(cf_filter_data_t *data)   /* I - Job and printer data */
{
  cf_logfunc_t     log = data->logfunc;   /* Log function */
  void             *ld = data->logdata;   /* log function data */

  if (data->ppdfile == NULL)
  {
    data->ppd = NULL;
    return (0);
  }

  if ((data->ppd = ppdOpenFile(data->ppdfile)) == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterLoadPPD: Could not load PPD file %s: %s",
		 data->ppdfile, strerror(errno));
    return (1);
  }

  data->ppd->cache = ppdCacheCreateWithPPD(data->ppd);
  ppdMarkDefaults(data->ppd);
  ppdMarkOptions(data->ppd, data->num_options, data->options);

  return (0);
}


/*
 * 'cfFilterFreePPD()' - After being done with the filter functions
 *                       Free the memory used by the PPD file data in
 *                       the data structure. If the pomiter to the PPD
 *                       file data "ppd" is NULL, do nothing.
 */

void
cfFilterFreePPD(cf_filter_data_t *data) /* I - Job and printer data */
{
  if (data->ppd == NULL)
    return;

  /* ppdClose() frees not only the main data structure but also the cache */
  ppdClose(data->ppd);
}


/*
 * 'cfFilterTee()' - This filter function is mainly for debugging. it
 *                 resembles the "tee" utility, passing through the
 *                 data unfiltered and copying it to a file. The file
 *                 name is simply given as parameter. This makes using
 *                 the function easy (add it as item of a filter chain
 *                 called via cfFilterChain()) and can even be used more
 *                 than once in the same filter chain (using different
 *                 file names). In case of write error to the copy
 *                 file, copying is stopped but the rest of the job is
 *                 passed on to the next filter. If NULL is supplied
 *                 as file name, the data is simply passed through
 *                 without getting copied.
 */

int                            /* O - Error status */
cfFilterTee(int inputfd,         /* I - File descriptor input stream */
	  int outputfd,        /* I - File descriptor output stream */
	  int inputseekable,   /* I - Is input stream seekable? (unused) */
	  cf_filter_data_t *data, /* I - Job and printer data */
	  void *parameters)    /* I - Filter-specific parameters (File name) */
{
  const char           *filename = (const char *)parameters;
  ssize_t	       bytes, total = 0;      /* Bytes read/written */
  char	               buffer[65536];         /* Read/write buffer */
  cf_logfunc_t     log = data->logfunc;   /* Log function */
  void                 *ld = data->logdata;   /* log function data */
  int                  teefd = -1;            /* File descriptor for "tee"ed
                                                 copy */


  (void)inputseekable;

  /* Open the "tee"ed copy file */
  if (filename)
    teefd = open(filename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);

  while ((bytes = read(inputfd, buffer, sizeof(buffer))) > 0)
  {
    total += bytes;
    if (log)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "cfFilterTee (%s): Passing on%s %d bytes, total %d bytes.",
	  filename, teefd >= 0 ? " and copying" : "", bytes, total);

    if (teefd >= 0)
      if (write(teefd, buffer, (size_t)bytes) != bytes)
      {
	if (log)
	  log(ld, CF_LOGLEVEL_ERROR,
	      "cfFilterTee (%s): Unable to write %d bytes to the copy, stopping copy, continuing job output.",
	      filename, (int)bytes);
	close(teefd);
	teefd = -1;
      }

    if (write(outputfd, buffer, (size_t)bytes) != bytes)
    {
      if (log)
	log(ld, CF_LOGLEVEL_ERROR,
	    "cfFilterTee (%s): Unable to pass on %d bytes.",
	    filename, (int)bytes);
      if (teefd >= 0)
	close(teefd);
      close(inputfd);
      close(outputfd);
      return (1);
    }
  }

  if (teefd >= 0)
    close(teefd);
  close(inputfd);
  close(outputfd);
  return (0);
}


/*
 * 'cfFilterPOpen()' - Pipe a stream to or from a filter function
 *                   Can be the input to or the output from the
 *                   filter function.
 */

int                              /* O - File decriptor */
cfFilterPOpen(cf_filter_function_t filter_func, /* I - Filter function */
	    int inputfd,         /* I - File descriptor input stream or -1 */
	    int outputfd,        /* I - File descriptor output stream or -1 */
	    int inputseekable,   /* I - Is input stream seekable? */
	    cf_filter_data_t *data, /* I - Job and printer data */
	    void *parameters,    /* I - Filter-specific parameters */
	    int *filter_pid)     /* O - PID of forked filter process */
{
  int		pipefds[2],          /* Pipes for filters */
		pid,		     /* Process ID of filter */
                ret,
                infd, outfd;         /* Temporary file descriptors */
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * Check file descriptors...
  */

  if (inputfd < 0 && outputfd < 0)
  {
    if (log)
      log(ld, CF_LOGLEVEL_ERROR,
	  "cfFilterPOpen: Either inputfd or outputfd must be < 0, not both");
    return (-1);
  }

  if (inputfd > 0 && outputfd > 0)
  {
    if (log)
      log(ld, CF_LOGLEVEL_ERROR,
	  "cfFilterPOpen: One of inputfd or outputfd must be < 0");
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
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPOpen: Could not create pipe for %s: %s",
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
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPOpen: Filter function completed with status %d.",
		 ret);
    exit(ret);

  } else if (pid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterPOpen: Filter function (PID %d) started.", pid);

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

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPOpen: Could not fork to start filter function: %s",
		 strerror(errno));
    return (-1);
  }
}


/*
 * 'cfFilterPClose()' - Close a piped stream created with
 *                    cfFilterPOpen().
 */

int                              /* O - Error status */
cfFilterPClose(int fd,             /* I - Pipe file descriptor */
	     int filter_pid,     /* I - PID of forked filter process */
	     cf_filter_data_t *data)
{
  int		status,		 /* Exit status */
                retval;		 /* Return value */
  cf_logfunc_t log = data->logfunc;
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
      log(ld, CF_LOGLEVEL_DEBUG,
	  "cfFilterPClose: Filter function (PID %d) stopped with an error: %s!",
	  filter_pid, strerror(errno));
    goto out;
  }

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
               "cfFilterPClose: Filter function (PID %d) exited with no errors.",
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
 * 'cfFilterChain()' - Call filter functions in a chain to do a data
 *                   format conversion which non of the individual
 *                   filter functions does
 */

int                              /* O - Error status */
cfFilterChain(int inputfd,         /* I - File descriptor input stream */
	    int outputfd,        /* I - File descriptor output stream */
	    int inputseekable,   /* I - Is input stream seekable? */
	    cf_filter_data_t *data, /* I - Job and printer data */
	    void *parameters)    /* I - Filter-specific parameters */
{
  cups_array_t  *filter_chain = (cups_array_t *)parameters;
  cf_filter_filter_in_chain_t *filter,  /* Current filter */
		*next;		     /* Next filter */
  int		current,	     /* Current filter */
		filterfds[2][2],     /* Pipes for filters */
		pid,		     /* Process ID of filter */
		status,		     /* Exit status */
		retval,		     /* Return value */
		ret;
  int		infd, outfd;         /* Temporary file descriptors */
  char          buf[4096];
  ssize_t       bytes;
  cups_array_t	*pids;		     /* Executed filters array */
  filter_function_pid_t	*pid_entry,  /* Entry in executed filters array */
		key;		     /* Search key for filters */
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Remove NULL filters...
  */

  for (filter = (cf_filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter;
       filter = (cf_filter_filter_in_chain_t *)cupsArrayNext(filter_chain)) {
    if (!filter->function) {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "cfFilterChain: Invalid filter: %s - Removing...",
		   filter->name ? filter->name : "Unspecified");
      cupsArrayRemove(filter_chain, filter);
    } else
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "cfFilterChain: Running filter: %s",
		   filter->name ? filter->name : "Unspecified");
  }

 /*
  * Empty filter chain -> Pass through the data unchanged
  */

  if (cupsArrayCount(filter_chain) == 0)
  {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterChain: No filter at all in chain, passing through the data.");
    retval = 0;
    while ((bytes = read(inputfd, buf, sizeof(buf))) > 0)
      if (write(outputfd, buf, bytes) < bytes)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterChain: Data write error: %s", strerror(errno));
	retval = 1;
	break;
      }
    if (bytes < 0)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterChain: Data read error: %s", strerror(errno));
      retval = 1;
    }
    close(inputfd);
    close(outputfd);
    return (retval);
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

  for (filter = (cf_filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter;
       filter = next, current = 1 - current) {
    next = (cf_filter_filter_in_chain_t *)cupsArrayNext(filter_chain);

    if (filterfds[1 - current][0] > 1) {
      close(filterfds[1 - current][0]);
      filterfds[1 - current][0] = -1;
    }
    if (filterfds[1 - current][1] > 1) {
      close(filterfds[1 - current][1]);
      filterfds[1 - current][1] = -1;
    }

    if (next) {
      if (pipe(filterfds[1 - current]) < 0) {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterChain: Could not create pipe for output of %s: %s",
		     filter->name ? filter->name : "Unspecified filter",
		     strerror(errno));
	return (1);
      }
      fcntl_add_cloexec(filterfds[1 - current][0]);
      fcntl_add_cloexec(filterfds[1 - current][1]);
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
      if (filterfds[current][1] > 1)
	close(filterfds[current][1]);
      if (filterfds[1 - current][0] > 1)
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
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterChain: %s completed with status %d.",
		   filter->name ? filter->name : "Unspecified filter", ret);
      exit(ret);

    } else if (pid > 0) {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "cfFilterChain: %s (PID %d) started.",
		   filter->name ? filter->name : "Unspecified filter", pid);

      pid_entry = malloc(sizeof(filter_function_pid_t));
      pid_entry->pid = pid;
      pid_entry->name = filter->name ? filter->name : "Unspecified filter";
      cupsArrayAdd(pids, pid_entry);
    } else {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterChain: Could not fork to start %s: %s",
		   filter->name ? filter->name : "Unspecified filter",
		   strerror(errno));
      break;
    }

    inputseekable = 0;
  }

 /*
  * Close remaining pipes...
  */

  if (filterfds[0][0] > 1)
    close(filterfds[0][0]);
  if (filterfds[0][1] > 1)
    close(filterfds[0][1]);
  if (filterfds[1][0] > 1)
    close(filterfds[1][0]);
  if (filterfds[1][1] > 1)
    close(filterfds[1][1]);

 /*
  * Wait for the children to exit...
  */

  retval = 0;

  while (cupsArrayCount(pids) > 0) {
    if ((pid = wait(&status)) < 0) {
      if (errno == EINTR && iscanceled && iscanceled(icd)) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterChain: Job canceled, killing filters ...");
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
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterChain: %s (PID %d) stopped with status %d",
		       pid_entry->name, pid, WEXITSTATUS(status));
	} else {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterChain: %s (PID %d) crashed on signal %d",
		       pid_entry->name, pid, WTERMSIG(status));
	}
	retval = 1;
      } else {
	if (log) log(ld, CF_LOGLEVEL_INFO,
		       "cfFilterChain: %s (PID %d) exited with no errors.",
		       pid_entry->name, pid);
      }

      free(pid_entry);
    }
  }

  cupsArrayDelete(pids);

  return (retval);
}


/*
 * 'get_env_var()' - Auxiliary function for cfFilterExternalCUPS(), gets value of
 *                   an environment variable in a list of environment variables
 *                   as used by the execve() function
 */

static char *             /* O - The value, NULL if variable is not in list */
get_env_var(char *name,   /* I - Name of environment variable to read */
	    char **env)   /* I - List of environment variable serttings */
{
  int i = 0;


  if (env)
    for (i = 0; env[i]; i ++)
      if (strncmp(env[i], name, strlen(name)) == 0 &&
	  strlen(env[i]) > strlen(name) &&
	  env[i][strlen(name)] == '=')
	return (env[i] + strlen(name) + 1);

  return (NULL);
}


/*
 * 'add_env_var()' - Auxiliary function for cfFilterExternalCUPS(), adds/sets
 *                   an environment variable in a list of environment variables
 *                   as used by the execve() function
 */

static int                /* O - Index of where the new value got inserted in
			         the list */
add_env_var(char *name,   /* I - Name of environment variable to set */
	    char *value,  /* I - Value of environment variable to set */
	    char ***env)  /* I - List of environment variable serttings */
{
  char *p;
  int i = 0,
      name_len;


  if (!name || !env || !name[0])
    return (-1);

  /* Assemble a "VAR=VALUE" string and the string length of "VAR" */
  if ((p = strchr(name, '=')) != NULL)
  {
    /* User supplied "VAR=VALUE" as name and NULL as value */
    if (value)
      return (-1);
    name_len = p - name;
    p = strdup(name);
  }
  else
  {
    /* User supplied variable name and value as the name and as the value */
    name_len = strlen(name);
    p = (char *)calloc(strlen(name) + (value ? strlen(value) : 0) + 2,
		       sizeof(char));
    sprintf(p, "%s=%s", name, (value ? value : ""));
  }

  /* Check whether we already have this variable in the list and update its
     value if it is there */
  if (*env)
    for (i = 0; (*env)[i]; i ++)
      if (strncmp((*env)[i], p, name_len) == 0 && (*env)[i][name_len] == '=')
      {
	free((*env)[i]);
	(*env)[i] = p;
	return (i);
      }

  /* Add the variable as new item to the list */
  *env = (char **)realloc(*env, (i + 2) * sizeof(char *));
  (*env)[i] = p;
  (*env)[i + 1] = NULL;
  return (i);
}


/*
 * 'sanitize_device_uri()' - Remove authentication info from a device URI
 */

static char *                           /* O - Sanitized URI */
sanitize_device_uri(const char *uri,	/* I - Device URI */
		    char *buf,          /* I - Buffer for output */
		    size_t bufsize)     /* I - Size of buffer */
{
  char	*start,				/* Start of data after scheme */
	*slash,				/* First slash after scheme:// */
	*ptr;				/* Pointer into user@host:port part */


  /* URI not supplied */
  if (!uri)
    return (NULL);

  /* Copy the device URI to a temporary buffer so we can sanitize any auth
   * info in it... */
  strncpy(buf, uri, bufsize);

  /* Find the end of the scheme:// part... */
  if ((ptr = strchr(buf, ':')) != NULL)
  {
    for (start = ptr + 1; *start; start ++)
      if (*start != '/')
        break;

    /* Find the next slash (/) in the URI... */
    if ((slash = strchr(start, '/')) == NULL)
      slash = start + strlen(start);	/* No slash, point to the end */

    /* Check for an @ sign before the slash... */
    if ((ptr = strchr(start, '@')) != NULL && ptr < slash)
    {
      /* Found an @ sign and it is before the resource part, so we have
	 an authentication string.  Copy the remaining URI over the
	 authentication string... */
      memmove(start, ptr + 1, strlen(ptr + 1) + 1);
    }
  }

  /* Return the sanitized URI... */
  return (buf);
}


/*
 * 'cfFilterExternalCUPS()' - Filter function which calls an external,
 *                          classic CUPS filter, for example a
 *                          (proprietary) printer driver which cannot
 *                          be converted to a filter function or is to
 *                          awkward or risky to convert for example
 *                          when the printer hardware is not available
 *                          for testing
 */

int                                     /* O - Error status */
cfFilterExternalCUPS(int inputfd,         /* I - File descriptor input stream */
		   int outputfd,        /* I - File descriptor output stream */
		   int inputseekable,   /* I - Is input stream seekable? */
		   cf_filter_data_t *data, /* I - Job and printer data */
		   void *parameters)    /* I - Filter-specific parameters */
{
  cf_filter_external_cups_t *params = (cf_filter_external_cups_t *)parameters;
  int           i;
  int           is_backend = 0;      /* Do we call a CUPS backend? */
  int		pid,		     /* Process ID of filter */
                stderrpid,           /* Process ID for stderr logging process */
                wpid;                /* PID reported as terminated */
  int		fd;		     /* Temporary file descriptor */
  int           backfd, sidefd;      /* file descriptors for back and side
                                        channels */
  int           stderrpipe[2];       /* Pipe to log stderr */
  cups_file_t   *fp;                 /* File pointer to read log lines */
  char          buf[2048];           /* Log line buffer */
  cf_loglevel_t log_level;       /* Log level of filter's log message */
  char          *ptr1, *ptr2,
                *msg,                /* Filter log message */
                *filter_name;        /* Filter name for logging */
  char          filter_path[1024];   /* Full path of the filter */
  char          **argv,		     /* Command line args for filter */
                **envp = NULL;       /* Environment variables for filter */
  int           num_all_options = 0;
  cups_option_t *all_options = NULL;
  char          job_id_str[16],
                copies_str[16],
                *options_str = NULL;
  cups_option_t *opt;
  int status = 65536;
  int wstatus;
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  if (!params->filter || !params->filter[0]) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternalCUPS: Filter executable path/command not specified");
    return (1);
  }

  /* Check whether back/side channel FDs are valid and not all-zero
     from calloc'ed filter_data */
  if (data->back_pipe[0] == 0 && data->back_pipe[1] == 0)
    data->back_pipe[0] = data->back_pipe[1] = -1;
  if (data->side_pipe[0] == 0 && data->side_pipe[1] == 0)
    data->side_pipe[0] = data->side_pipe[1] = -1;

  /* Select the correct end of the back/side channel pipes:
     [0] for filters, [1] for backends */
  is_backend = (params->is_backend ? 1 : 0);
  backfd = data->back_pipe[is_backend];
  sidefd = data->side_pipe[is_backend];

  /* Filter name for logging */
  if ((filter_name = strrchr(params->filter, '/')) != NULL)
    filter_name ++;
  else
    filter_name = (char *)params->filter;

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Copy the current environment variables and add some important ones
  * needed for correct execution of the CUPS filter (which is not running
  * out of CUPS here)
  */

  /* Some default environment variables from CUPS, will get overwritten
     if also defined in the environment in which the caller is started
     or in the parameters */
  add_env_var("CUPS_DATADIR", CUPS_DATADIR, &envp);
  add_env_var("CUPS_SERVERBIN", CUPS_SERVERBIN, &envp);
  add_env_var("CUPS_SERVERROOT", CUPS_SERVERROOT, &envp);
  add_env_var("CUPS_STATEDIR", CUPS_STATEDIR, &envp);
  add_env_var("SOFTWARE", "CUPS/2.4.99", &envp); /* Last CUPS with PPDs */

  /* Copy the environment in which the caller got started */
  if (environ)
    for (i = 0; environ[i]; i ++)
      add_env_var(environ[i], NULL, &envp);

  /* Set the environment variables given by the parameters */
  if (params->envp)
    for (i = 0; params->envp[i]; i ++)
      add_env_var(params->envp[i], NULL, &envp);

  /* Add CUPS_SERVERBIN to the beginning of PATH */
  ptr1 = get_env_var("PATH", envp);
  ptr2 = get_env_var("CUPS_SERVERBIN", envp);
  if (ptr2 && ptr2[0])
  {
    if (ptr1 && ptr1[0])
    {
      snprintf(buf, sizeof(buf), "%s/%s:%s",
	       ptr2, params->is_backend ? "backend" : "filter", ptr1);
      ptr1 = buf;
    }
    else
      ptr1 = ptr2;
    add_env_var("PATH", ptr1, &envp);
  }

  if (params->is_backend < 2) /* Not needed in discovery mode of backend */
  {
    /* Print queue name from filter data */
    if (data->printer)
      add_env_var("PRINTER", data->printer, &envp);
    else
      add_env_var("PRINTER", "Unknown", &envp);

    /* PPD file path/name from filter data, required for most CUPS filters */
    if (data->ppdfile)
      add_env_var("PPD", data->ppdfile, &envp);

    /* Device URI from parameters */
    if (params->is_backend && params->device_uri)
      add_env_var("DEVICE_URI", (char *)params->device_uri, &envp);
  }

  /* Determine full path for the filter */
  if (params->filter[0] == '/' ||
      (ptr1 = get_env_var("CUPS_SERVERBIN", envp)) == NULL || !ptr1[0])
    strncpy(filter_path, params->filter, sizeof(filter_path) - 1);
  else
    snprintf(filter_path, sizeof(filter_path), "%s/%s/%s", ptr1,
	     params->is_backend ? "backend" : "filter", params->filter);

  /* Log the resulting list of environment variable settings
     (with any authentication info removed)*/
  if (log)
  {
    for (i = 0; envp[i]; i ++)
      if (!strncmp(envp[i], "AUTH_", 5))
	log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternalCUPS (%s): envp[%d]: AUTH_%c****",
	    filter_name, i, envp[i][5]);
      else if (!strncmp(envp[i], "DEVICE_URI=", 11))
	log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternalCUPS (%s): envp[%d]: DEVICE_URI=%s",
	    filter_name, i, sanitize_device_uri(envp[i] + 11,
						buf, sizeof(buf)));
      else
	log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternalCUPS (%s): envp[%d]: %s",
	    filter_name, i, envp[i]);
  }

  if (params->is_backend < 2) {
   /*
    * Filter or backend for job execution
    */

   /*
    * Join the options from the filter data and from the parameters
    * If an option is present in both filter data and parameters, the
    * value in the filter data has priority
    */

    for (i = 0, opt = params->options; i < params->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);
    for (i = 0, opt = data->options; i < data->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);

   /*
    * Create command line arguments for the CUPS filter
    */

    argv = (char **)calloc(7, sizeof(char *));

    /* Numeric parameters */
    snprintf(job_id_str, sizeof(job_id_str) - 1, "%d",
	     data->job_id > 0 ? data->job_id : 1);
    snprintf(copies_str, sizeof(copies_str) - 1, "%d",
	     data->copies > 0 ? data->copies : 1);

    /* Options, build string of "Name1=Value1 Name2=Value2 ..." but use
       "Name" and "noName" instead for boolean options */
    for (i = 0, opt = all_options; i < num_all_options; i ++, opt ++) {
      if (strcasecmp(opt->value, "true") == 0 ||
	  strcasecmp(opt->value, "false") == 0) {
	options_str =
	  (char *)realloc(options_str,
			  ((options_str ? strlen(options_str) : 0) +
			   strlen(opt->name) +
			   (strcasecmp(opt->value, "false") == 0 ? 2 : 0) + 2) *
			  sizeof(char));
	if (i == 0)
	  options_str[0] = '\0';
	sprintf(options_str + strlen(options_str), " %s%s",
		(strcasecmp(opt->value, "false") == 0 ? "no" : ""), opt->name);
      } else {
	options_str =
	  (char *)realloc(options_str,
			  ((options_str ? strlen(options_str) : 0) +
			   strlen(opt->name) + strlen(opt->value) + 3) *
			  sizeof(char));
	if (i == 0)
	  options_str[0] = '\0';
	sprintf(options_str + strlen(options_str), " %s=%s", opt->name, opt->value);
      }
    }

    /* Find DEVICE_URI environment variable */
    if (params->is_backend && !params->device_uri)
      for (i = 0; envp[i]; i ++)
	if (strncmp(envp[i], "DEVICE_URI=", 11) == 0)
	  break;

    /* Add items to array */
    argv[0] = strdup((params->is_backend && params->device_uri ?
		      (char *)sanitize_device_uri(params->device_uri,
						  buf, sizeof(buf)) :
		      (params->is_backend && envp[i] ?
		       (char *)sanitize_device_uri(envp[i] + 11,
						   buf, sizeof(buf)) :
		       (data->printer ? data->printer :
			(char *)params->filter))));
    argv[1] = job_id_str;
    argv[2] = data->job_user ? data->job_user : "Unknown";
    argv[3] = data->job_title ? data->job_title : "Untitled";
    argv[4] = copies_str;
    argv[5] = options_str ? options_str + 1 : "";
    argv[6] = NULL;

    /* Log the arguments */
    if (log)
      for (i = 0; argv[i]; i ++)
	log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternalCUPS (%s): argv[%d]: %s",
	    filter_name, i, argv[i]);
  } else {
   /*
    * Backend in device discovery mode
    */

    argv = (char **)calloc(2, sizeof(char *));
    argv[0] = strdup((char *)params->filter);
    argv[1] = NULL;
  }

 /*
  * Execute the filter
  */

  if (pipe(stderrpipe) < 0) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternalCUPS (%s): Could not create pipe for stderr: %s",
		 filter_name, strerror(errno));
    return (1);
  }

  if ((pid = fork()) == 0) {
   /*
    * Child process goes here...
    *
    * Update stdin/stdout/stderr as needed...
    */

    if (inputfd != 0) {
      if (inputfd < 0) {
        inputfd = open("/dev/null", O_RDONLY);
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternalCUPS (%s): No input file descriptor supplied for CUPS filter - %s",
		   filter_name, strerror(errno));
      }

      if (inputfd > 0) {
	fcntl_add_cloexec(inputfd);
        if (dup2(inputfd, 0) < 0) {
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterExternalCUPS (%s): Failed to connect input file descriptor with CUPS filter's stdin - %s",
		       filter_name, strerror(errno));
	  goto fd_error;
	} else
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterExternalCUPS (%s): Connected input file descriptor %d to CUPS filter's stdin.",
		       filter_name, inputfd);
	close(inputfd);
      }
    } else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterExternalCUPS (%s): Input comes from stdin, letting the filter grab stdin directly",
		   filter_name);

    if (outputfd != 1) {
      if (outputfd < 0)
        outputfd = open("/dev/null", O_WRONLY);

      if (outputfd > 1) {
	fcntl_add_cloexec(outputfd);
	dup2(outputfd, 1);
	close(outputfd);
      }
    }

    if (strcasestr(params->filter, "gziptoany")) {
      /* Send stderr to the Nirwana if we are running gziptoany, as
	 gziptoany emits a false "PAGE: 1 1" */
      if ((fd = open("/dev/null", O_RDWR)) > 2) {
	fcntl_add_cloexec(fd);
	dup2(fd, 2);
	close(fd);
      } else
        close(fd);
    } else {
      /* Send stderr into pipe for logging */
      fcntl_add_cloexec(stderrpipe[1]);
      dup2(stderrpipe[1], 2);
      fcntl_add_nonblock(2);
    }
    close(stderrpipe[0]);
    close(stderrpipe[1]);

    if (params->is_backend < 2) { /* Not needed in discovery mode of backend */
      /* Back channel */
      if (backfd != 3 && backfd >= 0) {
	dup2(backfd, 3);
	close(backfd);
	fcntl_add_nonblock(3);
      } else if (backfd < 0) {
	if ((backfd = open("/dev/null", O_RDWR)) > 3) {
	  dup2(backfd, 3);
	  close(backfd);
	} else
	  close(backfd);
	fcntl_add_nonblock(3);
      }

      /* Side channel */
      if (sidefd != 4 && sidefd >= 0) {
	dup2(sidefd, 4);
	close(sidefd);
	fcntl_add_nonblock(4);
      } else if (sidefd < 0) {
	if ((sidefd = open("/dev/null", O_RDWR)) > 4) {
	  dup2(sidefd, 4);
	  close(sidefd);
	} else
	  close(sidefd);
	fcntl_add_nonblock(4);
      }
    }

   /*
    * Execute command...
    */

    execve(filter_path, argv, envp);

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternalCUPS (%s): Execution of %s %s failed - %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 filter_path, strerror(errno));

  fd_error:
    exit(errno);
  } else if (pid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterExternalCUPS (%s): %s (PID %d) started.",
		 filter_name, filter_path, pid);
  } else {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternalCUPS (%s): Unable to fork process for %s %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 filter_path);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }
  if (inputfd >= 0)
    close(inputfd);
  if (outputfd >= 0)
    close(outputfd);

 /*
  * Log the filter's stderr
  */

  if ((stderrpid = fork()) == 0) {
   /*
    * Child process goes here...
    */

    close(stderrpipe[1]);
    fp = cupsFileOpenFd(stderrpipe[0], "r");
    while (cupsFileGets(fp, buf, sizeof(buf)))
      if (log) {
	if (strncmp(buf, "DEBUG: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 7;
	} else if (strncmp(buf, "DEBUG2: ", 8) == 0) {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 8;
	} else if (strncmp(buf, "INFO: ", 6) == 0) {
	  log_level = CF_LOGLEVEL_INFO;
	  msg = buf + 6;
	} else if (strncmp(buf, "WARNING: ", 9) == 0) {
	  log_level = CF_LOGLEVEL_WARN;
	  msg = buf + 9;
	} else if (strncmp(buf, "ERROR: ", 7) == 0) {
	  log_level = CF_LOGLEVEL_ERROR;
	  msg = buf + 7;
	} else if (strncmp(buf, "PAGE: ", 6) == 0 ||
		   strncmp(buf, "ATTR: ", 6) == 0 ||
		   strncmp(buf, "STATE: ", 6) == 0 ||
		   strncmp(buf, "PPD: ", 6) == 0) {
	  log_level = CF_LOGLEVEL_CONTROL;
	  msg = buf;
	} else {
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf;
	}
	if (log_level == CF_LOGLEVEL_CONTROL)
	  log(ld, log_level, msg);
	else
	  log(ld, log_level, "cfFilterExternalCUPS (%s): %s", filter_name, msg);
      }
    cupsFileClose(fp);
    /* No need to close the fd stderrpipe[0], as cupsFileClose(fp) does this
       already */
    /* Ignore errors of the logging process */
    exit(0);
  } else if (stderrpid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterExternalCUPS (%s): Logging (PID %d) started.",
		 filter_name, stderrpid);
  } else {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternalCUPS (%s): Unable to fork process for logging",
		 filter_name);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }

  close(stderrpipe[0]);
  close(stderrpipe[1]);

 /*
  * Wait for filter and logging processes to finish
  */

  status = 0;

  while (pid > 0 || stderrpid > 0) {
    if ((wpid = wait(&wstatus)) < 0) {
      if (errno == EINTR && iscanceled && iscanceled(icd)) {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterExternalCUPS (%s): Job canceled, killing %s ...",
		     filter_name, params->is_backend ? "backend" : "filter");
	kill(pid, SIGTERM);
	pid = -1;
	kill(stderrpid, SIGTERM);
	stderrpid = -1;
	break;
      } else
	continue;
    }

    /* How did the filter terminate */
    if (wstatus) {
      if (WIFEXITED(wstatus)) {
	/* Via exit() anywhere or return() in the main() function */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternalCUPS (%s): %s (PID %d) stopped with status %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WEXITSTATUS(wstatus));
      } else {
	/* Via signal */
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternalCUPS (%s): %s (PID %d) crashed on signal %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WTERMSIG(wstatus));
      }
      status = 1;
    } else {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "cfFilterExternalCUPS (%s): %s (PID %d) exited with no errors.",
		   filter_name,
		   (wpid == pid ?
		    (params->is_backend ? "Backend" : "Filter") : "Logging"),
		   wpid);
    }
    if (wpid == pid)
      pid = -1;
    else  if (wpid == stderrpid)
      stderrpid = -1;
  }

 /*
  * Clean up
  */

 out:
  cupsFreeOptions(num_all_options, all_options);
  if (options_str)
    free(options_str);
  free(argv[0]);
  free(argv);
  for (i = 0; envp[i]; i ++)
    free(envp[i]);
  free(envp);

  return (status);
}


/*
 * 'cfFilterOpenBackAndSidePipes()' - Open the pipes for the back
 *                                  channel and the side channel, so
 *                                  that the filter functions can
 *                                  communicate with a backend. Only
 *                                  needed if a CUPS backend (either
 *                                  implemented as filter function or
 *                                  called via cfFilterExternalCUPS())
 *                                  is called with the same
 *                                  filter_data record as the
 *                                  filters. Usually to be called when
 *                                  populating the filter_data record.
 */

int                           /* O - 0 on success, -1 on error */
cfFilterOpenBackAndSidePipes(
    cf_filter_data_t *data)      /* O - FDs in filter_data record */
{
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * Initialize FDs...
  */

  data->back_pipe[0] = -1;
  data->back_pipe[1] = -1;
  data->side_pipe[0] = -1;
  data->side_pipe[1] = -1;

 /*
  * Create the back channel pipe...
  */

  if (pipe(data->back_pipe))
    goto out;

 /*
  * Set the "close on exec" flag on each end of the pipe...
  */

  if (fcntl_add_cloexec(data->back_pipe[0]))
    goto out;

  if (fcntl_add_cloexec(data->back_pipe[1]))
    goto out;

 /*
  * Create a socket pair as bi-directional pipe for the side channel...
  */

  if (socketpair(AF_LOCAL, SOCK_STREAM, 0, data->side_pipe))
    goto out;

 /*
  * Make the side channel FDs non-blocking...
  */

  if (fcntl_add_nonblock(data->side_pipe[0]))
    goto out;
  if (fcntl_add_nonblock(data->side_pipe[1]))
    goto out;

  if (fcntl_add_cloexec(data->side_pipe[0]))
    goto out;
  if (fcntl_add_cloexec(data->side_pipe[1]))
    goto out;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Pipes for back and side channels opened");

 /*
  * Return 0 indicating success...
  */

  return (0);

 out:

 /*
  * Clean up after failure...
  */

  if (log) log(ld, CF_LOGLEVEL_ERROR,
	       "Unable to open pipes for back and side channels");

  cfFilterCloseBackAndSidePipes(data);

  return (-1);
}


/*
 * 'cfFilterCloseBackAndSidePipes()' - Close the pipes for the back
 *                                   hannel and the side channel.
 *                                   sually to be called when done
 *                                   with the filter chain .
 */

void
cfFilterCloseBackAndSidePipes(
    cf_filter_data_t *data)      /* O - FDs in filter_data record */
{
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


 /*
  * close all valid FDs...
  */

  if (data->back_pipe[0] >= 0)
    close(data->back_pipe[0]);
  if (data->back_pipe[1] >= 0)
    close(data->back_pipe[1]);
  if (data->side_pipe[0] >= 0)
    close(data->side_pipe[0]);
  if (data->side_pipe[1] >= 0)
    close(data->side_pipe[1]);

 /*
  * ... and invalidate them
  */

  data->back_pipe[0] = -1;
  data->back_pipe[1] = -1;
  data->side_pipe[0] = -1;
  data->side_pipe[1] = -1;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Closed the pipes for back and side channels");
}


/*
 * 'cfFilterSetCommonOptions()' - Set common filter options for media size, etc.
 *                              based on PPD file
 */

void
cfFilterSetCommonOptions(
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
    cf_logfunc_t log,               /* I - Logging function,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page width: %.0f",
		   pagesize->width);
      corrected = 1;
    }
    if (pagesize->length > 0) 
      *PageLength = pagesize->length;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page length: %.0f",
		   pagesize->length);
      corrected = 1;
    }
    if (pagesize->top >= 0 && pagesize->top <= *PageLength) 
      *PageTop = pagesize->top;
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid value for page bottom margin: %.0f",
		   pagesize->bottom);
      if (*PageLength <= *PageBottom)
	*PageBottom = 0.0f;
      corrected = 1;
    }
    if (*PageBottom == *PageTop)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
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
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Invalid values for page margins: Left: %.0f; Right: %.0f",
		   *PageLeft, *PageRight);
      float swap = *PageLeft;
      *PageLeft = *PageRight;
      *PageRight = swap;
      corrected = 1;
    }

    if (corrected)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "PPD Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   pagesize->width, pagesize->length, pagesize->left,
		   pagesize->bottom, pagesize->right, pagesize->top);
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "Corrected Page = %.0fx%.0f; %.0f,%.0f to %.0f,%.0f",
		   *PageWidth, *PageLength, *PageLeft,
		   *PageBottom, *PageRight, *PageTop);
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
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
    cfFilterUpdatePageVars(*Orientation, PageLeft, PageRight,
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
 * 'cfFilterUpdatePageVars()' - Update the page variables for the orientation.
 */

void
cfFilterUpdatePageVars(int Orientation,
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
