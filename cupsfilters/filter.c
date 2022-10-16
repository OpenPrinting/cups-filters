//
// Filter functions support for cups-filters.
//
// Copyright © 2020 by Till Kamppeter.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//


//
// Include necessary headers...
//

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


//
// Type definitions
//

typedef struct filter_function_pid_s    // Filter in filter chain
{
  char          *name;                  // Filter executable name
  int           pid;                    // PID of filter process
} filter_function_pid_t;


//
// 'fcntl_add_cloexec()' - Add FD_CLOEXEC flag to the flags
//                         of a given file descriptor.
//

static int                // Return value of fcntl()
fcntl_add_cloexec(int fd) // File descriptor to add FD_CLOEXEC to
{
  return (fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC));
}


//
// 'fcntl_add_nonblock()' - Add O_NONBLOCK flag to the flags
//                          of a given file descriptor.
//

static int                 // Return value of fcntl()
fcntl_add_nonblock(int fd) // File descriptor to add O_NONBLOCK to
{
  return (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK));
}


//
// 'cfCUPSLogFunc()' - Output log messages on stderr, compatible to
//                     CUPS, meaning that the debug level is
//                     represented by a prefix like "DEBUG: ", "INFO:
//                     ", ...
//

void
cfCUPSLogFunc(void *data,
	     cf_loglevel_t level,
	     const char *message,
	     ...)
{
  va_list arglist;


  (void)data; // No extra data needed

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


//
// 'cfCUPSIsCanceledFunc()' - Return 1 if the job is canceled, which
//                            is the case when the integer pointed at
//                            by data is not zero.
//

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


void *                                     // O - Extension record which got
                                           //     replaced, NULL if there was
                                           //	  no record under this name,
                                           //	  the added record is the one
                                           //     there, or no record was
                                           //     added. If not NULL the
                                           //     returned record should usually
                                           //     be deleted or freed.
cfFilterDataAddExt(cf_filter_data_t *data, // I - Filter data record
		   const char *name,       // I - Name of extension
		   void *ext)              // I - Extension record to be added
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
    return (NULL);
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
    return (NULL);
}


//
// 'cfFilterGetEnvVar()' - Auxiliary function for cfFilterExternal(),
//                         gets value of an environment variable in a
//                         list of environment variables as used by
//                         the execve() function
//

char *				// O - The value, NULL if variable is not in
				//     list
cfFilterGetEnvVar(char *name,	// I - Name of environment variable to read
		  char **env)	// I - List of environment variable serttings
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


//
// 'cfFilterAddEnvVar()' - Auxiliary function for cfFilterExternal(),
//                   adds/sets an environment variable in a list of
//                   environment variables as used by the execve()
//                   function
//

int				// O - Index of where the new value got
				//     inserted in the list
cfFilterAddEnvVar(char *name,   // I - Name of environment variable to set
		  char *value,  // I - Value of environment variable to set
		  char ***env)  // I - List of environment variable serttings
{
  char *p;
  int i = 0,
      name_len;


  if (!name || !env || !name[0])
    return (-1);

  // Assemble a "VAR=VALUE" string and the string length of "VAR"
  if ((p = strchr(name, '=')) != NULL)
  {
    // User supplied "VAR=VALUE" as name and NULL as value
    if (value)
      return (-1);
    name_len = p - name;
    p = strdup(name);
  }
  else
  {
    // User supplied variable name and value as the name and as the value
    name_len = strlen(name);
    p = (char *)calloc(strlen(name) + (value ? strlen(value) : 0) + 2,
		       sizeof(char));
    sprintf(p, "%s=%s", name, (value ? value : ""));
  }

  // Check whether we already have this variable in the list and update its
  // value if it is there
  if (*env)
    for (i = 0; (*env)[i]; i ++)
      if (strncmp((*env)[i], p, name_len) == 0 && (*env)[i][name_len] == '=')
      {
	free((*env)[i]);
	(*env)[i] = p;
	return (i);
      }

  // Add the variable as new item to the list
  *env = (char **)realloc(*env, (i + 2) * sizeof(char *));
  (*env)[i] = p;
  (*env)[i + 1] = NULL;
  return (i);
}


//
// 'cfFilterTee()' - This filter function is mainly for debugging. it
//                   resembles the "tee" utility, passing through the
//                   data unfiltered and copying it to a file. The
//                   file name is simply given as parameter. This
//                   makes using the function easy (add it as item of
//                   a filter chain called via cfFilterChain()) and
//                   can even be used more than once in the same
//                   filter chain (using different file names). In
//                   case of write error to the copy file, copying is
//                   stopped but the rest of the job is passed on to
//                   the next filter. If NULL is supplied as file
//                   name, the data is simply passed through without
//                   getting copied.
//

int                                 // O - Error status
cfFilterTee(int inputfd,            // I - File descriptor input stream
	    int outputfd,           // I - File descriptor output stream
	    int inputseekable,      // I - Is input stream seekable? (unused)
	    cf_filter_data_t *data, // I - Job and printer data
	    void *parameters)       // I - Filter-specific parameters (File
                                    //     name)
{
  const char           *filename = (const char *)parameters;
  ssize_t	       bytes, total = 0;      // Bytes read/written
  char	               buffer[65536];         // Read/write buffer
  cf_logfunc_t         log = data->logfunc;   // Log function
  void                 *ld = data->logdata;   // log function data
  int                  teefd = -1;            // File descriptor for "tee"ed
                                              // copy


  (void)inputseekable;

  // Open the "tee"ed copy file
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


//
// 'cfFilterPOpen()' - Pipe a stream to or from a filter function Can
//                     be the input to or the output from the filter
//                     function.
//

int                                   // O - File decriptor
cfFilterPOpen(cf_filter_function_t filter_func,
	                              // I - Filter function
	      int inputfd,            // I - File descriptor input stream or -1
	      int outputfd,           // I - File descriptor output stream or -1
	      int inputseekable,      // I - Is input stream seekable?
	      cf_filter_data_t *data, // I - Job and printer data
	      void *parameters,       // I - Filter-specific parameters
	      int *filter_pid)        // O - PID of forked filter process
{
  int		pipefds[2],           // Pipes for filters
		pid,		      // Process ID of filter
                ret,
                infd, outfd;          // Temporary file descriptors
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


  //
  // Check file descriptors...
  //

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

  //
  // Ignore broken pipe signals...
  //

  signal(SIGPIPE, SIG_IGN);

  //
  // Open a pipe ...
  //

  if (pipe(pipefds) < 0) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPOpen: Could not create pipe for %s: %s",
		 inputfd < 0 ? "input" : "output",
		 strerror(errno));
    return (-1);
  }

  if ((pid = fork()) == 0) {
    //
    // Child process goes here...
    //
    // Update input and output FDs as needed...
    //

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

    //
    // Execute filter function...
    //

    ret = (filter_func)(infd, outfd, inputseekable, data, parameters);

    //
    // Close file descriptor and terminate the sub-process...
    //

    close(infd);
    close(outfd);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterPOpen: Filter function completed with status %d.",
		 ret);
    exit(ret);

  } else if (pid > 0) {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterPOpen: Filter function (PID %d) started.", pid);

    //
    // Save PID for waiting for or terminating the sub-process
    //

    *filter_pid = pid;

    //
    // Return file descriptor to stream to or from
    //

    if (inputfd < 0) {
      close(pipefds[0]);
      return (pipefds[1]);
    } else {
      close(pipefds[1]);
      return (pipefds[0]);
    }

  } else {

    //
    // fork() error
    //

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterPOpen: Could not fork to start filter function: %s",
		 strerror(errno));
    return (-1);
  }
}


//
// 'cfFilterPClose()' - Close a piped stream created with
//                      cfFilterPOpen().
//

int                                // O - Error status
cfFilterPClose(int fd,             // I - Pipe file descriptor
	       int filter_pid,     // I - PID of forked filter process
	       cf_filter_data_t *data)
{
  int		status,		 // Exit status
                retval;		 // Return value
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;


  //
  // close the stream...
  //

  close(fd);

  //
  // Wait for the child process to exit...
  //

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

  // How did the filter function terminate
  if (WIFEXITED(status))
    // Via exit() anywhere or return() in the main() function
    retval = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    // Via signal
    retval = 256 * WTERMSIG(status);

 out:
  return (retval);
}


//
// 'compare_filter_pids()' - Compare two filter PIDs...
//

static int					// O - Result of comparison
compare_filter_pids(filter_function_pid_t *a,	// I - First filter
		    filter_function_pid_t *b)	// I - Second filter
{
  return (a->pid - b->pid);
}


//
// 'cfFilterChain()' - Call filter functions in a chain to do a data
//                     format conversion which non of the individual
//                     filter functions does
//

int                                // O - Error status
cfFilterChain(int inputfd,         // I - File descriptor input stream
	      int outputfd,        // I - File descriptor output stream
	      int inputseekable,   // I - Is input stream seekable?
	      cf_filter_data_t *data,
	                           // I - Job and printer data
	      void *parameters)    // I - Filter-specific parameters
{
  cups_array_t  *filter_chain = (cups_array_t *)parameters;
  cf_filter_filter_in_chain_t *filter,  // Current filter
		*next;		     // Next filter
  int		current,	     // Current filter
		filterfds[2][2],     // Pipes for filters
		pid,		     // Process ID of filter
		status,		     // Exit status
		retval,		     // Return value
		ret;
  int		infd, outfd;         // Temporary file descriptors
  char          buf[4096];
  ssize_t       bytes;
  cups_array_t	*pids;		     // Executed filters array
  filter_function_pid_t	*pid_entry,  // Entry in executed filters array
		key;		     // Search key for filters
  cf_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  //
  // Ignore broken pipe signals...
  //

  signal(SIGPIPE, SIG_IGN);

  //
  // Remove NULL filters...
  //

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

  //
  // Empty filter chain -> Pass through the data unchanged
  //

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

  //
  // Execute all of the filters...
  //

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
      //
      // Child process goes here...
      //
      // Update input and output FDs as needed...
      //

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

      //
      // Execute filter function...
      //

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

  //
  // Close remaining pipes...
  //

  if (filterfds[0][0] > 1)
    close(filterfds[0][0]);
  if (filterfds[0][1] > 1)
    close(filterfds[0][1]);
  if (filterfds[1][0] > 1)
    close(filterfds[1][0]);
  if (filterfds[1][1] > 1)
    close(filterfds[1][1]);

  //
  // Wait for the children to exit...
  //

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


//
// 'sanitize_device_uri()' - Remove authentication info from a device URI
//

static char *                           // O - Sanitized URI
sanitize_device_uri(const char *uri,	// I - Device URI
		    char *buf,          // I - Buffer for output
		    size_t bufsize)     // I - Size of buffer
{
  char	*start,				// Start of data after scheme
	*slash,				// First slash after scheme://
	*ptr;				// Pointer into user@host:port part


  // URI not supplied
  if (!uri)
    return (NULL);

  // Copy the device URI to a temporary buffer so we can sanitize any auth
  // info in it...
  strncpy(buf, uri, bufsize);

  // Find the end of the scheme:// part...
  if ((ptr = strchr(buf, ':')) != NULL)
  {
    for (start = ptr + 1; *start; start ++)
      if (*start != '/')
        break;

    // Find the next slash (/) in the URI...
    if ((slash = strchr(start, '/')) == NULL)
      slash = start + strlen(start);	// No slash, point to the end

    // Check for an @ sign before the slash...
    if ((ptr = strchr(start, '@')) != NULL && ptr < slash)
    {
      // Found an @ sign and it is before the resource part, so we have
      // an authentication string.  Copy the remaining URI over the
      // authentication string...
      memmove(start, ptr + 1, strlen(ptr + 1) + 1);
    }
  }

  // Return the sanitized URI...
  return (buf);
}


//
// 'cfFilterExternal()' - Filter function which calls an external
//                        classic CUPS filter or System V interface
//                        script, for example a (proprietary) printer
//                        driver which cannot be converted to a filter
//                        function or if it is too awkward or risky to
//                        convert for example when the printer
//                        hardware is not available for testing
//

int                                        // O - Error status
cfFilterExternal(int inputfd,              // I - File descriptor input stream
		 int outputfd,             // I - File descriptor output stream
		 int inputseekable,        // I - Is input stream seekable?
		 cf_filter_data_t *data,   // I - Job and printer data
		 void *parameters)         // I - Filter-specific parameters
{
  cf_filter_external_t *params = (cf_filter_external_t *)parameters;
  int           i;
  int           is_backend = 0;      // Do we call a CUPS backend?
  int		pid,		     // Process ID of filter
                stderrpid,           // Process ID for stderr logging process
                wpid;                // PID reported as terminated
  int		fd;		     // Temporary file descriptor
  int           backfd, sidefd;      // file descriptors for back and side
                                     // channels
  int           stderrpipe[2];       // Pipe to log stderr
  cups_file_t   *fp;                 // File pointer to read log lines
  char          tmp_name[BUFSIZ] = "";
  char          buf[2048];           // Log line buffer
  cf_loglevel_t log_level;           // Log level of filter's log message
  char          *ptr1, *ptr2,
                *msg,                // Filter log message
                *filter_name;        // Filter name for logging
  char          filter_path[1024];   // Full path of the filter
  char          **argv,		     // Command line args for filter
                **envp = NULL;       // Environment variables for filter
  int           num_all_options = 0;
  cups_option_t *all_options = NULL;
  char          job_id_str[16],
                copies_str[16],
                *options_str = NULL;
  cups_option_t *opt;
  int           status = 65536;
  int           wstatus;
  cf_logfunc_t  log = data->logfunc;
  void          *ld = data->logdata;
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void          *icd = data->iscanceleddata;


  if (!params->filter || !params->filter[0])
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternal: Filter executable path/command not specified");
    return (1);
  }

  // Check whether back/side channel FDs are valid and not all-zero
  // from calloc'ed filter_data
  if (data->back_pipe[0] == 0 && data->back_pipe[1] == 0)
    data->back_pipe[0] = data->back_pipe[1] = -1;
  if (data->side_pipe[0] == 0 && data->side_pipe[1] == 0)
    data->side_pipe[0] = data->side_pipe[1] = -1;

  // Select the correct end of the back/side channel pipes:
  // [0] for filters, [1] for backends
  is_backend = (params->exec_mode > 0 ? 1 : 0);
  backfd = data->back_pipe[is_backend];
  sidefd = data->side_pipe[is_backend];

  // Filter name for logging
  if ((filter_name = strrchr(params->filter, '/')) != NULL)
    filter_name ++;
  else
    filter_name = (char *)params->filter;

  //
  // Ignore broken pipe signals...
  //

  signal(SIGPIPE, SIG_IGN);

  //
  // Copy the current environment variables and add the ones from the
  // parameters
  //

  // Copy the environment in which the caller got started
  if (environ)
    for (i = 0; environ[i]; i ++)
      cfFilterAddEnvVar(environ[i], NULL, &envp);

  // Set the environment variables given by the parameters
  if (params->envp)
    for (i = 0; params->envp[i]; i ++)
      cfFilterAddEnvVar(params->envp[i], NULL, &envp);

  // Add CUPS_SERVERBIN to the beginning of PATH
  ptr1 = cfFilterGetEnvVar("PATH", envp);
  ptr2 = cfFilterGetEnvVar("CUPS_SERVERBIN", envp);
  if (ptr2 && ptr2[0])
  {
    if (ptr1 && ptr1[0])
    {
      snprintf(buf, sizeof(buf), "%s/%s:%s",
	       ptr2, params->exec_mode > 0 ? "backend" : "filter", ptr1);
      ptr1 = buf;
    }
    else
      ptr1 = ptr2;
    cfFilterAddEnvVar("PATH", ptr1, &envp);
  }

  // Determine full path for the filter
  if (params->filter[0] == '/' ||
      (ptr1 = cfFilterGetEnvVar("CUPS_SERVERBIN", envp)) == NULL || !ptr1[0])
    strncpy(filter_path, params->filter, sizeof(filter_path) - 1);
  else
    snprintf(filter_path, sizeof(filter_path), "%s/%s/%s", ptr1,
	     params->exec_mode > 0 ? "backend" : "filter", params->filter);

  // Log the resulting list of environment variable settings
  // (with any authentication info removed)
  if (log)
  {
    for (i = 0; envp[i]; i ++)
      if (!strncmp(envp[i], "AUTH_", 5))
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterExternal (%s): envp[%d]: AUTH_%c****",
	    filter_name, i, envp[i][5]);
      else if (!strncmp(envp[i], "DEVICE_URI=", 11))
	log(ld, CF_LOGLEVEL_DEBUG,
	    "cfFilterExternal (%s): envp[%d]: DEVICE_URI=%s",
	    filter_name, i, sanitize_device_uri(envp[i] + 11,
						buf, sizeof(buf)));
      else
	log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternal (%s): envp[%d]: %s",
	    filter_name, i, envp[i]);
  }

  if (params->exec_mode < 2)
  {
    //
    // Filter or backend for job execution
    //

    //
    // Join the options from the filter data and from the parameters
    // If an option is present in both filter data and parameters, the
    // value in the filter data has priority
    //

    for (i = 0, opt = params->options; i < params->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);
    for (i = 0, opt = data->options; i < data->num_options; i ++, opt ++)
      num_all_options = cupsAddOption(opt->name, opt->value, num_all_options,
				      &all_options);

    //
    // Create command line arguments for the CUPS filter
    //

    if (params->exec_mode >= 0)
      // CUPS filter or backend

      // CUPS filter allow input via stdin, so no 6th command line
      // argument needed
      argv = (char **)calloc(7, sizeof(char *));
    else
    {
      // System V interface script

      // Needs input via file name as 6th command line
      // argument, not via stdin
      argv = (char **)calloc(8, sizeof(char *));

      fd = cupsTempFd(tmp_name, sizeof(tmp_name));
      if (fd < 0)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternal: Can't create temporary file.");
	goto out;
      }
      int bytes;
      while ((bytes = read(inputfd, buf, sizeof(buf))) > 0)
      {
	if (write(fd, buf, bytes) != bytes)
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterExternal: Can't copy input data to temporary file.");
	  close(fd);
	  goto out;
	}
      }
      close(fd);
      close(inputfd);
      inputfd = open("/dev/null", O_RDONLY);
    }

    // Numeric parameters
    snprintf(job_id_str, sizeof(job_id_str) - 1, "%d",
	     data->job_id > 0 ? data->job_id : 1);
    snprintf(copies_str, sizeof(copies_str) - 1, "%d",
	     data->copies > 0 ? data->copies : 1);

    // Options, build string of "Name1=Value1 Name2=Value2 ..." but use
    // "Name" and "noName" instead for boolean options
    for (i = 0, opt = all_options; i < num_all_options; i ++, opt ++)
    {
      if (strcasecmp(opt->value, "true") == 0 ||
	  strcasecmp(opt->value, "false") == 0)
      {
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
      }
      else
      {
	options_str =
	  (char *)realloc(options_str,
			  ((options_str ? strlen(options_str) : 0) +
			   strlen(opt->name) + strlen(opt->value) + 3) *
			  sizeof(char));
	if (i == 0)
	  options_str[0] = '\0';
	sprintf(options_str + strlen(options_str), " %s=%s", opt->name,
		opt->value);
      }
    }

    // Find DEVICE_URI environment variable
    if (params->exec_mode > 0)
      for (i = 0; envp[i]; i ++)
	if (strncmp(envp[i], "DEVICE_URI=", 11) == 0)
	  break;

    // Add items to array
    argv[0] = strdup((params->exec_mode > 0 && envp[i] ?
		      (char *)sanitize_device_uri(envp[i] + 11,
						  buf, sizeof(buf)) :
		       (data->printer ? data->printer :
			(char *)params->filter)));
    argv[1] = job_id_str;
    argv[2] = data->job_user ? data->job_user : "Unknown";
    argv[3] = data->job_title ? data->job_title : "Untitled";
    argv[4] = copies_str;
    argv[5] = options_str ? options_str + 1 : "";
    if (params->exec_mode >= 0)
      // CUPS Filter/backend: Input from stdin
      argv[6] = NULL;
    else
    {
      // System V Interface: Input file from 6th argument
      argv[6] = tmp_name;
      argv[7] = NULL;
    }
  }
  else
  {
    //
    // Backend in device discovery mode
    //

    argv = (char **)calloc(2, sizeof(char *));
    argv[0] = strdup((char *)params->filter);
    argv[1] = NULL;
  }

  // Log the arguments
  if (log)
    for (i = 0; argv[i]; i ++)
      log(ld, CF_LOGLEVEL_DEBUG, "cfFilterExternal (%s): argv[%d]: %s",
	  filter_name, i, argv[i]);

  //
  // Execute the filter
  //

  if (pipe(stderrpipe) < 0)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternal (%s): Could not create pipe for stderr: %s",
		 filter_name, strerror(errno));
    return (1);
  }

  if ((pid = fork()) == 0)
  {
    //
    // Child process goes here...
    //
    // Update stdin/stdout/stderr as needed...
    //

    if (inputfd != 0)
    {
      if (inputfd < 0)
      {
        inputfd = open("/dev/null", O_RDONLY);
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternal (%s): No input file descriptor supplied for CUPS filter - %s",
		     filter_name, strerror(errno));
      }

      if (inputfd > 0)
      {
	fcntl_add_cloexec(inputfd);
        if (dup2(inputfd, 0) < 0)
	{
	  if (log) log(ld, CF_LOGLEVEL_ERROR,
		       "cfFilterExternal (%s): Failed to connect input file descriptor with CUPS filter's stdin - %s",
		       filter_name, strerror(errno));
	  goto fd_error;
	} else
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterExternal (%s): Connected input file descriptor %d to CUPS filter's stdin.",
		       filter_name, inputfd);
	close(inputfd);
      }
    }
    else
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterExternal (%s): Input file descriptor is stdin, no redirection needed.",
		   filter_name);

    if (tmp_name[0])
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterExternal (%s): Input comes from temporary file %s, supplied as 6th command line argument.",
		   filter_name, tmp_name);
    
    if (outputfd != 1)
    {
      if (outputfd < 0)
        outputfd = open("/dev/null", O_WRONLY);

      if (outputfd > 1) {
	fcntl_add_cloexec(outputfd);
	dup2(outputfd, 1);
	close(outputfd);
      }
    }

    if (strcasestr(params->filter, "gziptoany"))
    {
      // Send stderr to the Nirwana if we are running gziptoany, as
      // gziptoany emits a false "PAGE: 1 1"
      if ((fd = open("/dev/null", O_RDWR)) > 2)
      {
	fcntl_add_cloexec(fd);
	dup2(fd, 2);
	close(fd);
      } else
        close(fd);
    }
    else
    {
      // Send stderr into pipe for logging
      fcntl_add_cloexec(stderrpipe[1]);
      dup2(stderrpipe[1], 2);
      fcntl_add_nonblock(2);
    }
    close(stderrpipe[0]);
    close(stderrpipe[1]);

    if (params->exec_mode < 2) // Not needed in discovery mode of backend
    {
      // Back channel
      if (backfd != 3 && backfd >= 0)
      {
	dup2(backfd, 3);
	close(backfd);
	fcntl_add_nonblock(3);
      }
      else if (backfd < 0)
      {
	if ((backfd = open("/dev/null", O_RDWR)) > 3)
	{
	  dup2(backfd, 3);
	  close(backfd);
	}
	else
	  close(backfd);
	fcntl_add_nonblock(3);
      }

      // Side channel
      if (sidefd != 4 && sidefd >= 0)
      {
	dup2(sidefd, 4);
	close(sidefd);
	fcntl_add_nonblock(4);
      }
      else if (sidefd < 0)
      {
	if ((sidefd = open("/dev/null", O_RDWR)) > 4)
	{
	  dup2(sidefd, 4);
	  close(sidefd);
	} else
	  close(sidefd);
	fcntl_add_nonblock(4);
      }
    }

    //
    // Execute command...
    //

    execve(filter_path, argv, envp);

    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternal (%s): Execution of %s %s failed - %s",
		 filter_name, params->exec_mode > 0 ? "backend" : "filter",
		 filter_path, strerror(errno));

  fd_error:
    exit(errno);
  }
  else if (pid > 0)
  {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterExternal (%s): %s (PID %d) started.",
		 filter_name, filter_path, pid);
  }
  else
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternal (%s): Unable to fork process for %s %s",
		 filter_name, params->exec_mode > 0 ? "backend" : "filter",
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

  //
  // Log the filter's stderr
  //

  if ((stderrpid = fork()) == 0)
  {
    //
    // Child process goes here...
    //

    close(stderrpipe[1]);
    fp = cupsFileOpenFd(stderrpipe[0], "r");
    while (cupsFileGets(fp, buf, sizeof(buf)))
      if (log)
      {
	if (strncmp(buf, "DEBUG: ", 7) == 0)
	{
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 7;
	}
	else if (strncmp(buf, "DEBUG2: ", 8) == 0)
	{
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf + 8;
	}
	else if (strncmp(buf, "INFO: ", 6) == 0)
	{
	  log_level = CF_LOGLEVEL_INFO;
	  msg = buf + 6;
	}
	else if (strncmp(buf, "WARNING: ", 9) == 0)
	{
	  log_level = CF_LOGLEVEL_WARN;
	  msg = buf + 9;
	}
	else if (strncmp(buf, "ERROR: ", 7) == 0)
	{
	  log_level = CF_LOGLEVEL_ERROR;
	  msg = buf + 7;
	}
	else if (strncmp(buf, "PAGE: ", 6) == 0 ||
		 strncmp(buf, "ATTR: ", 6) == 0 ||
		 strncmp(buf, "STATE: ", 7) == 0 ||
		 strncmp(buf, "PPD: ", 5) == 0)
	{
	  log_level = CF_LOGLEVEL_CONTROL;
	  msg = buf;
	}
	else
	{
	  log_level = CF_LOGLEVEL_DEBUG;
	  msg = buf;
	}
	if (log_level == CF_LOGLEVEL_CONTROL)
	  log(ld, log_level, msg);
	else
	  log(ld, log_level, "cfFilterExternal (%s): %s",
	      filter_name, msg);
      }
    cupsFileClose(fp);
    // No need to close the fd stderrpipe[0], as cupsFileClose(fp) does this
    // already
    // Ignore errors of the logging process
    exit(0);
  }
  else if (stderrpid > 0)
  {
    if (log) log(ld, CF_LOGLEVEL_INFO,
		 "cfFilterExternal (%s): Logging (PID %d) started.",
		 filter_name, stderrpid);
  }
  else
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterExternal (%s): Unable to fork process for logging",
		 filter_name);
    close(stderrpipe[0]);
    close(stderrpipe[1]);
    status = 1;
    goto out;
  }

  close(stderrpipe[0]);
  close(stderrpipe[1]);

  //
  // Wait for filter and logging processes to finish
  //

  status = 0;

  while (pid > 0 || stderrpid > 0)
  {
    if ((wpid = wait(&wstatus)) < 0)
    {
      if (errno == EINTR && iscanceled && iscanceled(icd))
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterExternal (%s): Job canceled, killing %s ...",
		     filter_name, params->exec_mode > 0 ? "backend" : "filter");
	kill(pid, SIGTERM);
	pid = -1;
	kill(stderrpid, SIGTERM);
	stderrpid = -1;
	break;
      }
      else
	continue;
    }

    // How did the filter terminate
    if (wstatus)
    {
      if (WIFEXITED(wstatus))
      {
	// Via exit() anywhere or return() in the main() function
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternal (%s): %s (PID %d) stopped with status %d",
		     filter_name,
		     (wpid == pid ?
		      (params->exec_mode > 0 ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WEXITSTATUS(wstatus));
      }
      else
      {
	// Via signal
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterExternal (%s): %s (PID %d) crashed on signal %d",
		     filter_name,
		     (wpid == pid ?
		      (params->exec_mode > 0 ? "Backend" : "Filter") :
		      "Logging"),
		     wpid, WTERMSIG(wstatus));
      }
      status = 1;
    }
    else
    {
      if (log) log(ld, CF_LOGLEVEL_INFO,
		   "cfFilterExternal (%s): %s (PID %d) exited with no errors.",
		   filter_name,
		   (wpid == pid ?
		    (params->exec_mode > 0 ? "Backend" : "Filter") : "Logging"),
		   wpid);
    }
    if (wpid == pid)
      pid = -1;
    else  if (wpid == stderrpid)
      stderrpid = -1;
  }

  //
  // Clean up
  //

 out:
  if (params->exec_mode < 0)
    unlink(tmp_name);
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


//
// 'cfFilterOpenBackAndSidePipes()' - Open the pipes for the back
//                                    channel and the side channel, so
//                                    that the filter functions can
//                                    communicate with a backend. Only
//                                    needed if a CUPS backend (either
//                                    implemented as filter function
//                                    or called via
//                                    cfFilterExternal()) is called
//                                    with the same filter_data record
//                                    as the filters. Usually to be
//                                    called when populating the
//                                    filter_data record.
//

int                                                  // O - 0 on success,
                                                     //     -1 on error
cfFilterOpenBackAndSidePipes(cf_filter_data_t *data) // O - FDs in filter_data
                                                     //     record
{
  cf_logfunc_t log = data->logfunc;
  void         *ld = data->logdata;


  //
  // Initialize FDs...
  //

  data->back_pipe[0] = -1;
  data->back_pipe[1] = -1;
  data->side_pipe[0] = -1;
  data->side_pipe[1] = -1;

  //
  // Create the back channel pipe...
  //

  if (pipe(data->back_pipe))
    goto out;

  //
  // Set the "close on exec" flag on each end of the pipe...
  //

  if (fcntl_add_cloexec(data->back_pipe[0]))
    goto out;

  if (fcntl_add_cloexec(data->back_pipe[1]))
    goto out;

  //
  // Create a socket pair as bi-directional pipe for the side channel...
  //

  if (socketpair(AF_LOCAL, SOCK_STREAM, 0, data->side_pipe))
    goto out;

  //
  // Make the side channel FDs non-blocking...
  //

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

  //
  // Return 0 indicating success...
  //

  return (0);

 out:

  //
  // Clean up after failure...
  //

  if (log) log(ld, CF_LOGLEVEL_ERROR,
	       "Unable to open pipes for back and side channels");

  cfFilterCloseBackAndSidePipes(data);

  return (-1);
}


//
// 'cfFilterCloseBackAndSidePipes()' - Close the pipes for the back
//                                     hannel and the side channel.
//                                     sually to be called when done
//                                     with the filter chain .
//

void
cfFilterCloseBackAndSidePipes(cf_filter_data_t *data) // I - FDs in filter_data
                                                      //     record
{
  cf_logfunc_t log = data->logfunc;
  void         *ld = data->logdata;


  //
  // close all valid FDs...
  //

  if (data->back_pipe[0] >= 0)
    close(data->back_pipe[0]);
  if (data->back_pipe[1] >= 0)
    close(data->back_pipe[1]);
  if (data->side_pipe[0] >= 0)
    close(data->side_pipe[0]);
  if (data->side_pipe[1] >= 0)
    close(data->side_pipe[1]);

  //
  // ... and invalidate them
  //

  data->back_pipe[0] = -1;
  data->back_pipe[1] = -1;
  data->side_pipe[0] = -1;
  data->side_pipe[1] = -1;

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "Closed the pipes for back and side channels");
}
