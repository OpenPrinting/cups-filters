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
  va_end(arglist);
  fputc('\n', stderr);
  fflush(stderr);
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
  filter_data.ppd = filter_data.ppdfile ?
                    ppdOpenFile(filter_data.ppdfile) : NULL;
                                       /* Load PPD file */
  filter_data.back_pipe[0] = 3;        /* CUPS uses file descriptor 3 for */
  filter_data.back_pipe[1] = 3;        /* the back channel */
  filter_data.side_pipe[0] = 4;        /* CUPS uses file descriptor 4 for */
  filter_data.side_pipe[1] = 4;        /* the side channel */
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

  retval = filter(inputfd, 1, inputseekable, &filter_data, parameters);

 /*
  * Clean up
  */

  cupsFreeOptions(num_options, options);
  if (filter_data.ppd)
    ppdClose(filter_data.ppd);

  return retval;
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
 * 'add_env_var()' - Auxiliary function for filterExternalCUPS(), adds/sets
 *                   an environment variable in a list of environment variables
 *                   as used by the execve() function
 */

int                       /* O - Index of where the new value got inserted in
			         the list */
add_env_var(char *name,   /* I - Name of environment variable to set */
	    char *value,  /* I - Value of environment variable to set */
	    char ***env)  /* I - List of environment variable serttings */
{
  char *p;
  int i = 0,
      name_len;


  if (!name || !env)
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
      if (strncmp((*env)[i], p, name_len) == 0)
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

char *                                  /* O - Sanitized URI */
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
 * 'filterExternalCUPS()' - Filter function which calls an external,
 *                          classic CUPS filter, for example a
 *                          (proprietary) printer driver which cannot
 *                          be converted to a filter function or is to
 *                          awkward or risky to convert for example
 *                          when the printer hardware is not available
 *                          for testing
 */

int                                     /* O - Error status */
filterExternalCUPS(int inputfd,         /* I - File descriptor input stream */
		   int outputfd,        /* I - File descriptor output stream */
		   int inputseekable,   /* I - Is input stream seekable? */
		   filter_data_t *data, /* I - Job and printer data */
		   void *parameters)    /* I - Filter-specific parameters */
{
  filter_external_cups_t *params = (filter_external_cups_t *)parameters;
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
  filter_loglevel_t log_level;       /* Log level of filter's log message */
  char          *msg,                /* Filter log message */
                *filter_name;        /* Filter name for logging */
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
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  if (!params->filter || !params->filter[0]) {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterExternalCUPS: Filter executable path/command not specified");
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

  /* Copy the environment variables in which the caller got started */
  if (environ)
    for (i = 0; environ[i]; i ++)
      add_env_var(environ[i], NULL, &envp);

  /* Set the environment variables given by the parameters */
  if (params->envp)
    for (i = 0; params->envp[i]; i ++)
      add_env_var(params->envp[i], NULL, &envp);

  if (params->is_backend < 2) /* Not needed in discovery mode of backend */
  {
    /* Print queue name from filter data */
    if (data->printer)
      add_env_var("PRINTER", data->printer, &envp);

    /* PPD file path/name from filter data, required for most CUPS filters */
    if (data->ppdfile)
      add_env_var("PPD", data->ppdfile, &envp);

    /* Device URI from parameters */
    if (params->is_backend && params->device_uri)
      add_env_var("DEVICE_URI", (char *)params->device_uri, &envp);
  }

  /* Log the resulting list of environment variable settings
     (with any authentication info removed)*/
  if (log)
  {
    for (i = 0; envp[i]; i ++)
      if (!strncmp(envp[i], "AUTH_", 5))
	log(ld, FILTER_LOGLEVEL_DEBUG, "filterExternalCUPS (%s): envp[%d]: AUTH_%c****",
	    filter_name, i, envp[i][5]);
      else if (!strncmp(envp[i], "DEVICE_URI=", 11))
	log(ld, FILTER_LOGLEVEL_DEBUG, "filterExternalCUPS (%s): envp[%d]: DEVICE_URI=%s",
	    filter_name, i, sanitize_device_uri(envp[i] + 11,
						buf, sizeof(buf)));
      else
	log(ld, FILTER_LOGLEVEL_DEBUG, "filterExternalCUPS (%s): envp[%d]: %s",
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
    snprintf(job_id_str, sizeof(job_id_str) - 1, "%d", data->job_id);
    snprintf(copies_str, sizeof(copies_str) - 1, "%d", data->copies);

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
    argv[2] = data->job_user;
    argv[3] = data->job_title;
    argv[4] = copies_str;
    argv[5] = options_str ? options_str + 1 : "";
    argv[6] = NULL;

    /* Log the arguments */
    if (log)
      for (i = 0; argv[i]; i ++)
	log(ld, FILTER_LOGLEVEL_DEBUG, "filterExternalCUPS (%s): argv[%d]: %s",
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
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterExternalCUPS (%s): Could not create pipe for stderr: %s",
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
      if (inputfd < 0)
        inputfd = open("/dev/null", O_RDONLY);

      if (inputfd > 0) {
        dup2(inputfd, 0);
	close(inputfd);
      }
    }

    if (outputfd != 1) {
      if (outputfd < 0)
        outputfd = open("/dev/null", O_WRONLY);

      if (outputfd > 1) {
	dup2(outputfd, 1);
	close(outputfd);
      }
    }

    if (strcasestr(params->filter, "gziptoany")) {
      /* Send stderr to the Nirwana if we are running gziptoany, as
	 gziptoany emits a false "PAGE: 1 1" */
      if ((fd = open("/dev/null", O_RDWR)) > 2) {
	dup2(fd, 2);
	close(fd);
      } else
        close(fd);
    } else
      /* Send stderr into pipe for logging */
      dup2(stderrpipe[1], 2);
    fcntl(2, F_SETFL, O_NDELAY);
    close(stderrpipe[0]);
    close(stderrpipe[1]);

    if (params->is_backend < 2) { /* Not needed in discovery mode of backend */
      /* Back channel */
      if (backfd != 3 && backfd >= 0) {
	dup2(backfd, 3);
	close(backfd);
	fcntl(3, F_SETFL, O_NDELAY);
      } else if (backfd < 0) {
	if ((backfd = open("/dev/null", O_RDWR)) > 3) {
	  dup2(backfd, 3);
	  close(backfd);
	}
	else
	  close(backfd);
	fcntl(3, F_SETFL, O_NDELAY);
      }

      /* Side channel */
      if (sidefd != 4 && sidefd >= 0) {
	dup2(sidefd, 4);
	close(sidefd);
	fcntl(4, F_SETFL, O_NDELAY);
      } else if (sidefd < 0) {
	if ((sidefd = open("/dev/null", O_RDWR)) > 4) {
	  dup2(sidefd, 4);
	  close(sidefd);
	} else
	  close(sidefd);
	fcntl(4, F_SETFL, O_NDELAY);
      }
    }

   /*
    * Execute command...
    */

    execve(params->filter, argv, envp);

    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterExternalCUPS (%s): Execution of %s %s failed - %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 params->filter, strerror(errno));

    exit(errno);
  } else if (pid > 0) {
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "filterExternalCUPS (%s): %s (PID %d) started.",
		 filter_name, params->filter, pid);
  } else {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterExternalCUPS (%s): Unable to fork process for %s %s",
		 filter_name, params->is_backend ? "backend" : "filter",
		 params->filter);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }

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
	  log_level = FILTER_LOGLEVEL_DEBUG;
	  msg = buf + 7;
	} else if (strncmp(buf, "DEBUG2: ", 8) == 0) {
	  log_level = FILTER_LOGLEVEL_DEBUG;
	  msg = buf + 8;
	} else if (strncmp(buf, "INFO: ", 6) == 0) {
	  log_level = FILTER_LOGLEVEL_INFO;
	  msg = buf + 6;
	} else if (strncmp(buf, "WARNING: ", 9) == 0) {
	  log_level = FILTER_LOGLEVEL_WARN;
	  msg = buf + 9;
	} else if (strncmp(buf, "ERROR: ", 7) == 0) {
	  log_level = FILTER_LOGLEVEL_ERROR;
	  msg = buf + 7;
	} else if (strncmp(buf, "PAGE: ", 6) == 0 ||
		   strncmp(buf, "ATTR: ", 6) == 0 ||
		   strncmp(buf, "STATE: ", 6) == 0 ||
		   strncmp(buf, "PPD: ", 6) == 0) {
	  log_level = FILTER_LOGLEVEL_CONTROL;
	  msg = buf;
	} else {
	  log_level = FILTER_LOGLEVEL_DEBUG;
	  msg = buf;
	}
	if (log_level == FILTER_LOGLEVEL_CONTROL)
	  log(ld, log_level, msg);
	else
	  log(ld, log_level, "filterExternalCUPS (%s): %s", filter_name, msg);
      }
    cupsFileClose(fp);
    /* No need to close the fd stderrpipe[0], as cupsFileClose(fp) does this
       already */
    /* Ignore errors of the logging process */
    exit(0);
  } else if (stderrpid > 0) {
    if (log) log(ld, FILTER_LOGLEVEL_INFO,
		 "filterExternalCUPS (%s): Logging (PID %d) started.",
		 filter_name, stderrpid);
  } else {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "filterExternalCUPS (%s): Unable to fork process for logging",
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
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "filterExternalCUPS (%s): Job canceled, killing %s ...",
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
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "filterExternalCUPS (%s): %s (PID %d) stopped with status %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WEXITSTATUS(wstatus));
	status = WEXITSTATUS(wstatus);
      } else {
	/* Via signal */
	if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		     "filterExternalCUPS (%s): %s (PID %d) crashed on signal %d",
		     filter_name,
		     (wpid == pid ?
		      (params->is_backend ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WTERMSIG(wstatus));
	status = 256 * WTERMSIG(wstatus);
      }
    } else {
      if (log) log(ld, FILTER_LOGLEVEL_INFO,
		   "filterExternalCUPS (%s): %s (PID %d) exited with no errors.",
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
 * 'filterOpenBackAndSidePipes()' - Open the pipes for the back
 *                                  channel and the side channel, so
 *                                  that the filter functions can
 *                                  communicate with a backend. Only
 *                                  needed if a CUPS backend (either
 *                                  implemented as filter function or
 *                                  called via filterExternalCUPS())
 *                                  is called with the same
 *                                  filter_data record as the
 *                                  filters. Usually to be called when
 *                                  populating the filter_data record.
 */

int                           /* O - 0 on success, -1 on error */
filterOpenBackAndSidePipes(
    filter_data_t *data)      /* O - FDs in filter_data record */
{
  filter_logfunc_t log = data->logfunc;
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

  if (fcntl(data->back_pipe[0], F_SETFD,
	    fcntl(data->back_pipe[0], F_GETFD) | FD_CLOEXEC))
    goto out;

  if (fcntl(data->back_pipe[1], F_SETFD,
	    fcntl(data->back_pipe[1], F_GETFD) | FD_CLOEXEC))
    goto out;

 /*
  * Create a socket pair as bi-directional pipe for the side channel...
  */

  if (socketpair(AF_LOCAL, SOCK_STREAM, 0, data->side_pipe))
    goto out;

 /*
  * Make the side channel FDs non-blocking...
  */

  if (fcntl(data->side_pipe[0], F_SETFL,
	    fcntl(data->side_pipe[0], F_GETFL) | O_NONBLOCK))
    goto out;
  if (fcntl(data->side_pipe[1], F_SETFL,
	    fcntl(data->side_pipe[1], F_GETFL) | O_NONBLOCK))
    goto out;

  if (fcntl(data->side_pipe[0], F_SETFD,
	    fcntl(data->side_pipe[0], F_GETFD) | FD_CLOEXEC))
    goto out;
  if (fcntl(data->side_pipe[1], F_SETFD,
	    fcntl(data->side_pipe[1], F_GETFD) | FD_CLOEXEC))
    goto out;

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "Pipes for back and side channels opened");

 /*
  * Return 0 indicating success...
  */

  return (0);

 out:

 /*
  * Clean up after failure...
  */

  if (log) log(ld, FILTER_LOGLEVEL_ERROR,
	       "Unable to open pipes for back and side channels");

  filterCloseBackAndSidePipes(data);

  return (-1);
}


/*
 * 'filterCloseBackAndSidePipes()' - Close the pipes for the back
 *                                   hannel and the side channel.
 *                                   sually to be called when done
 *                                   with the filter chain .
 */

void
filterCloseBackAndSidePipes(
    filter_data_t *data)      /* O - FDs in filter_data record */
{
  filter_logfunc_t log = data->logfunc;
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

  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
	       "Closed the pipes for back and side channels");
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
