/*
 * implicitclass backend for implementing an implicit-class-like behavior
 * of redundant print servers managed by cups-browsed.
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

/*
 * Local globals...
 */

#define IMPLICIT_CLASS_DEST_HOST_FILE "/var/cache/cups/cups-browsed-dest-host-%s"
static int		job_canceled = 0;
					/* Set to 1 on SIGTERM */

/*
 * Local functions... */

static void		sigterm_handler(int sig);


/*
 * 'main()' - Browse for printers.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  const char	*device_uri;            /* URI with which we were called */
  char queue_name[1024],
       filename[1024];
  FILE *fp;
  char *ptr1, *ptr2;
  int i, n;
  char dest_host[1024];	/* Destination host */
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

  if (argc >= 6)
  {
    if ((device_uri = getenv("DEVICE_URI")) == NULL)
    {
      if (!argv || !argv[0] || !strchr(argv[0], ':'))
	return (-1);

      device_uri = argv[0];
    }
    if ((ptr1 = strchr(device_uri, ':')) == NULL) {
    }
    ptr1 ++;
    strncpy(queue_name, ptr1, sizeof(queue_name));
    snprintf(filename, sizeof(filename), IMPLICIT_CLASS_DEST_HOST_FILE,
	     queue_name);
    for (i = 0; i < 40; i++) {
      /* Wait up to 20 sec for cups-browsed to supply the destination host */
      /* Try reading the file where cups-browsed has deposited the destination
	 host */
      fp = fopen(filename, "r");
      if (fp == NULL)
	goto failed;
      ptr1 = dest_host;
      /* Destination host is between double quotes, as double quotes are
	 illegal in host names one easily recognizes whether the file is
	 complete and avoids accepting a partially written host name */
      n = fscanf(fp, "\"%s", ptr1);
      fclose(fp);
      if (n == 1 && strlen(ptr1) > 0) {
	if ((ptr2 = strchr((const char *)ptr1, '"')) != NULL) {
	  *ptr2 = '\0';
	  break;
	}
      }
    failed:
      /* Pause half a second before next attempt */
      usleep(500000);
    }

    /* Delete host name file after having read it once, so that we wait for
       cups-browsed's decision again on the next job */
    unlink(filename);
    
    if (i >= 40) {
      /* Timeout, no useful data from cups-browsed received */
      fprintf(stderr, "ERROR: No destination host name supplied by cups-browsed for printer \"%s\", is cups-browsed running?\n",
	      queue_name);
      return (CUPS_BACKEND_STOP);
    }
    
    if (!strcmp(dest_host, "NO_DEST_FOUND")) {
      /* All remote queues are either disabled or not accepting jobs, let
	 CUPS retry after the usual interval */
      fprintf(stderr, "ERROR: No suitable destination host found by cups-browsed.\n");
      return (CUPS_BACKEND_RETRY);
    } else if (!strcmp(dest_host, "ALL_DESTS_BUSY")) {
      /* We queue on the client and all remote queues are busy, so we wait
	 5 sec  and check again then */
      fprintf(stderr, "DEBUG: No free destination host found by cups-browsed, retrying in 5 sec.\n");
      sleep(5);
      return (CUPS_BACKEND_RETRY_CURRENT);
    } else {
      /* We have the destination host name now, do the job */
      char server_str[1024];
      const char *title;
      int num_options = 0;
      cups_option_t *options = NULL;
      int fd, job_id;
      char buffer[8192];

      fprintf(stderr, "DEBUG: Received destination host name from cups-browsed: %s\n",
	      dest_host);
      /* Instead of fdeeding the job into the IPP backend, we re-print it into
	 the server's CUPS queue. This way the job gets spooled on the server
	 and we are not blocked until the job is printed. So a subsequent job
	 will be immediately processed and sent out to another server */
      /* Set destination server */
      snprintf(server_str, sizeof(server_str), "%s:%d", dest_host,
	       ippPort());
      cupsSetServer(server_str);
      /* Parse the command line option and prepare them for the new print
	 job */
      cupsSetUser(argv[2]);
      title = argv[3];
      if (title == NULL)
      {
	if (argc == 7) {
	  if ((title = strrchr(argv[6], '/')) != NULL)
	    title ++;
	  else
	    title = argv[6];
	} else
	  title = "(stdin)";
      }
      num_options = cupsAddOption("copies", argv[4], num_options, &options);
      num_options = cupsParseOptions(argv[5], num_options, &options);
      if (argc == 7)
	fd = open(argv[6], O_RDONLY);
      else
	fd = 0; /* stdin */
      
      /* Queue the job directly on the server */
      if ((job_id = cupsCreateJob(CUPS_HTTP_DEFAULT, queue_name,
				  title ? title : "(stdin)",
				  num_options, options)) > 0) {
	http_status_t       status;         /* Write status */
	const char          *format;        /* Document format */
	ssize_t             bytes;          /* Bytes read */

	if (cupsGetOption("raw", num_options, options))
	  format = CUPS_FORMAT_RAW;
	else if ((format = cupsGetOption("document-format", num_options,
					 options)) == NULL)
	  format = CUPS_FORMAT_AUTO;
	
	status = cupsStartDocument(CUPS_HTTP_DEFAULT, queue_name, job_id, NULL,
				   format, 1);

	while (status == HTTP_CONTINUE &&
	       (bytes = read(fd, buffer, sizeof(buffer))) > 0)
	  status = cupsWriteRequestData(CUPS_HTTP_DEFAULT, buffer, (size_t)bytes);

	if (status != HTTP_CONTINUE) {
	  fprintf(stderr, "ERROR: %s: Unable to queue the print data - %s. Retrying.",
		  argv[0], httpStatus(status));
	  cupsFinishDocument(CUPS_HTTP_DEFAULT, queue_name);
	  cupsCancelJob2(CUPS_HTTP_DEFAULT, queue_name, job_id, 0);
	  return (CUPS_BACKEND_RETRY_CURRENT);
	}

	if (cupsFinishDocument(CUPS_HTTP_DEFAULT, queue_name) != IPP_OK) {
	  fprintf(stderr, "ERROR: %s: Unable to complete the job - %s. Retrying.",
		  argv[0], cupsLastErrorString());
	  cupsCancelJob2(CUPS_HTTP_DEFAULT, queue_name, job_id, 0);
	  return (CUPS_BACKEND_RETRY_CURRENT);
	}
      }

      if (job_id < 1) {
	fprintf(stderr, "ERROR: %s: Unable to create job - %s. Retrying.",
		argv[0], cupsLastErrorString());
	return (CUPS_BACKEND_RETRY_CURRENT);
      }

      return (CUPS_BACKEND_OK);
    }
  }
  else if (argc != 1)
  {
    fprintf(stderr,
	    "Usage: %s job-id user title copies options [file]",
	    argv[0]);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * No discovery mode at all for this backend
  */

  return (CUPS_BACKEND_OK);
}


/*
 * 'sigterm_handler()' - Handle termination signals.
 */

static void
sigterm_handler(int sig)		/* I - Signal number (unused) */
{
  (void)sig;

  if (job_canceled)
    _exit(CUPS_BACKEND_OK);
  else
    job_canceled = 1;
}

