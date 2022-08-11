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


static cf_filter_data_ext_t *
get_filter_data_ext_entry(cups_array_t *ext_array,
			  const char *name)
{
  cf_filter_data_ext_t *entry;

  if (!ext_array || !name)
    return (NULL);

  for (entry = (cf_filter_data_ext_t *)cupsArrayFirst(ext_array);
       entry;
       entry = (cf_filter_data_ext_t *)cupsArrayNext(ext_array))
    if (strcmp(entry->name, name) == 0)
      break;

  return (entry);
}


void *                                     /* O - Extension record which got
					          replaced, NULL if there was
						  no record under this name,
						  the added record is the one
						  there, or no record was
						  added. If not NULL the
						  returned record should usually
						  be deleted or freed. */
cfFilterDataAddExt(cf_filter_data_t *data, /* I - Filter data record */
		   const char *name,       /* I - Name of extension */
		   void *ext)              /* I - Extension record to be added*/
{
  cf_filter_data_ext_t *entry;
  void *old_ext = NULL;

  if (!data || !name || !ext)
    return (NULL);

  if (data->extension == NULL)
    data->extension = cupsArrayNew(NULL, NULL);

  if (data->extension == NULL)
    return (NULL);

  if ((entry = get_filter_data_ext_entry(data->extension, name)) != NULL)
  {
    old_ext = entry->ext;
    entry->ext = ext;
  }
  else
  {
    entry = (cf_filter_data_ext_t *)calloc(1, sizeof(cf_filter_data_ext_t));
    if (entry)
    {
      entry->name = strdup(name);
      entry->ext = ext;
      cupsArrayAdd(data->extension, entry);
    }
  }

  return (old_ext);
}


void *
cfFilterDataGetExt(cf_filter_data_t *data,
		   const char *name)
{
  cf_filter_data_ext_t *entry;

  if (!data || !name || data->extension == NULL)
    return (NULL);

  if ((entry = get_filter_data_ext_entry(data->extension, name)) != NULL)
    return (entry->ext);
  else
    return NULL;
}


void *
cfFilterDataRemoveExt(cf_filter_data_t *data,
		      const char *name)
{
  cf_filter_data_ext_t *entry;
  void *ext = NULL;

  if (!data || !name || data->extension == NULL)
    return (NULL);

  if ((entry = get_filter_data_ext_entry(data->extension, name)) != NULL)
  {
    ext = entry->ext;
    cupsArrayRemove(data->extension, entry);
    free(entry->name);
    free(entry);
    if (cupsArrayCount(data->extension) == 0)
    {
      cupsArrayDelete(data->extension);
      data->extension = NULL;
    }
    return (ext);
  }
  else
    return NULL;
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
