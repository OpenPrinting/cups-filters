/*
 * beh ("Backend Error Handler") wrapper backend to extend the possibilities
 * of handling errors of backends.
 *
 * Copyright 2015 by Till Kamppeter
 *
 * This is based on dnssd.c of CUPS
 * dnssd.c copyright notice is follows:
 *
 * Copyright 2008-2015 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.
 */

/*
 * Include necessary headers.
 */

#include "backend-private.h"
#include <cups/array.h>
#include <ctype.h>
#include <sys/wait.h>

/*
 * Local globals...
 */

static volatile int	job_canceled = 0; /* Set to 1 on SIGTERM */

/*
 * Local functions...
 */

static int		call_backend(char *uri, int argc, char **argv,
				     char *tempfile);
static void		sigterm_handler(int sig);


/*
 * 'main()' - Browse for printers.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[]) {			/* I - Command-line arguments */
  char *uri, *ptr, *filename;
  char tmpfilename[1024], buf[8192];
  int dd, att, delay, retval;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

 /*
  * Don't buffer stderr, and catch SIGTERM...
  */

  setbuf(stderr, NULL);

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, sigterm_handler);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, sigterm_handler);
#endif /* HAVE_SIGSET */

 /*
  * Check command-line...
  */

  if (argc == 1) {
    if ((ptr = strrchr(argv[0], '/')) != NULL)
      ptr ++;
    else
      ptr = argv[0];
    printf("network %s \"Unknown\" \"Backend Error Handler\"\n",
	   ptr);
    return (CUPS_BACKEND_OK);
  } else if (argc < 6) {
    fprintf(stderr,
	    "Usage: %s job-id user title copies options [file]\n",
	    argv[0]);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * Read out the parameters
  */

  uri = getenv("DEVICE_URI");
  if (!uri) {
    fprintf(stderr,
	    "ERROR: No device URI supplied!");
    return (CUPS_BACKEND_FAILED);
  }

  ptr = strstr(uri, ":/");
  if (!ptr) goto bad_uri;
  ptr += 2;
  if (*ptr == '0')
    dd = 0;
  else if (*ptr == '1')
    dd = 1;
  else
    goto bad_uri;
  ptr ++;
  if (*ptr != '/') goto bad_uri;
  ptr ++;
  att = 0;
  while (isdigit(*ptr)) {
    att = att * 10 + (int)(*ptr) - 48;
    ptr ++;
  }
  if (*ptr != '/') goto bad_uri;
  ptr ++;
  delay = 0;
  while (isdigit(*ptr)) {
    delay = delay * 10 + (int)(*ptr) - 48;
    ptr ++;
  }
  if (*ptr != '/') goto bad_uri;
  ptr ++;
  fprintf(stderr,
	  "DEBUG: beh: Don't disable: %d; Attempts: %d; Delay: %d; Destination URI: %s\n",
	  dd, att, delay, ptr);

 /*
  * If reading from stdin, write everything into a temporary file
  */

  if (argc == 6) {
    char *tmpdir;
    int fd;
    FILE *tmpfile;
    size_t bytes;

    tmpdir = getenv("TMPDIR");
    if (!tmpdir)
      tmpdir = "/tmp";
    snprintf(tmpfilename, sizeof(tmpfilename), "%s/beh-XXXXXX", tmpdir);
    fd = mkstemp(tmpfilename);
    if (fd < 0) {
      fprintf(stderr,
	      "ERROR: beh: Could not create temporary file: %s\n",
	      strerror(errno));
      return (CUPS_BACKEND_FAILED);
    }
    tmpfile = fdopen(fd, "r+");
    while ((bytes = fread(buf, 1, sizeof(buf), stdin)))
      fwrite(buf, 1, bytes, tmpfile);
    fclose(tmpfile);
		    
    filename = tmpfilename;
  } else {
    tmpfilename[0] = '\0';
    filename = argv[6];
  }

 /*
  * Do it!
  */

  while ((retval = call_backend(ptr, argc, argv, filename)) !=
	 CUPS_BACKEND_OK &&
	 !job_canceled) {
    if (att > 0) {
      att --;
      if (att == 0)
	break;
    }
    if (delay > 0)
      sleep (delay);
  }

  if (strlen(tmpfilename) > 0)
    unlink(tmpfilename);

 /*
  * Return the exit value of the backend only if requested
  */

  if (!dd)
    return (retval);
  else
    return (CUPS_BACKEND_OK);

 /*
  * Error handling
  */

 bad_uri:

  fprintf(stderr,
	  "ERROR: URI must be \"beh:/<dd>/<att>/<delay>/<original uri>\"!\n");
  return (CUPS_BACKEND_FAILED);
}


/*
 * 'call_backend()' - Execute the command line of the destination backend
 */

static int
call_backend(char *uri,                 /* I - URI of final destination */
	     int  argc,                 /* I - Number of command line
	                                       arguments */
	     char **argv,		/* I - Command-line arguments */
	     char *filename) {          /* I - File name of input data */
  const char	*cups_serverbin;	/* Location of programs */
  char          *backend_argv[8];	/* Arguments for backend */
  char		scheme[1024],           /* Scheme from URI */
                *ptr,			/* Pointer into scheme */
		backend_path[2048];	/* Backend path */
  int           pid = 0, 		/* Process ID of backend */
                wait_pid,		/* Process ID from wait() */
                wait_status, 		/* Status from child */
                retval = 0;
  int           bytes;

 /*
  * Build the backend command line...
  */

  scheme[0] = '\0';
  strncat(scheme, uri, sizeof(scheme) - 1);
  if ((ptr = strchr(scheme, ':')) != NULL)
    *ptr = '\0';
  else {
    fprintf(stderr,
	    "ERROR: beh: Invalid URI, no colon (':') to mark end of scheme part.\n");
    exit (CUPS_BACKEND_FAILED);
  }
  if (strchr(scheme, '/')) {
    fprintf(stderr,
	    "ERROR: beh: Invalid URI, scheme contains a slash ('/').\n");
    exit (CUPS_BACKEND_FAILED);
  }
  if (!strcmp(scheme, ".") || !strcmp(scheme, "..")) {
    fprintf(stderr,
	    "ERROR: beh: Invalid URI, scheme (\"%s\") is a directory.\n",
	    scheme);
    exit (CUPS_BACKEND_FAILED);
  }
  if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
    cups_serverbin = CUPS_SERVERBIN;

  if (!strncasecmp(uri, "file:", 5) || uri[0] == '/') {
    fprintf(stderr,
	    "ERROR: beh: Direct output into a file not supported.\n");
    exit (CUPS_BACKEND_FAILED);
  }

  backend_argv[0] = uri;
  backend_argv[1] = argv[1];
  backend_argv[2] = argv[2];
  backend_argv[3] = argv[3];
  /* Apply number of copies only if beh was called with a file name
     and not with the print data in stdin, as backends should handle
     copies only if they are called with a file name */
  backend_argv[4] = (argc == 6 ? "1" : argv[4]);
  backend_argv[5] = argv[5];
  backend_argv[6] = filename;
  backend_argv[7] = NULL;

  bytes = snprintf(backend_path, sizeof(backend_path),
		   "%s/backend/%s", cups_serverbin, scheme);
  if (bytes < 0 || bytes >= sizeof(backend_path))
  {
    fprintf(stderr,
	    "ERROR: beh: Invalid scheme (\"%s\"), could not determing backend path.\n",
	    scheme);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * Overwrite the device URI and run the actual backend...
  */

  setenv("DEVICE_URI", uri, 1);

  fprintf(stderr,
	  "DEBUG: beh: Executing backend command line \"%s '%s' '%s' '%s' '%s' '%s' %s\"...\n",
	  backend_path, backend_argv[1], backend_argv[2], backend_argv[3],
	  backend_argv[4], backend_argv[5], backend_argv[6]);
  fprintf(stderr,
	  "DEBUG: beh: Using device URI: %s\n",
	  uri);

  if ((pid = fork()) == 0) {
   /*
    * Child comes here...
    */

    /* Run the backend */
    execv(backend_path, backend_argv);

    fprintf(stderr, "ERROR: Unable to execute backend command line: %s\n",
	    strerror(errno));

    exit(1);
  } else if (pid < 0) {
   /*
    * Unable to fork!
    */

    return (CUPS_BACKEND_FAILED);
  }

  while ((wait_pid = wait(&wait_status)) < 0 && errno == EINTR);

  if (wait_pid >= 0 && wait_status) {
    if (WIFEXITED(wait_status))
      retval = WEXITSTATUS(wait_status);
    else if (WTERMSIG(wait_status) != SIGTERM)
      retval = WTERMSIG(wait_status);
    else
      retval = 0;
  }

  return (retval);
}


/*
 * 'sigterm_handler()' - Handle termination signals.
 */

static void
sigterm_handler(int sig) {		/* I - Signal number (unused) */
  (void)sig;

  const char * const msg = "DEBUG: beh: Job canceled.\n";
  /* The if() is to eliminate the return value and silence the warning
     about an unused return value. */
  if (write(2, msg, strlen(msg)));

  if (job_canceled)
    _exit(CUPS_BACKEND_OK);
  else
    job_canceled = 1;
}
