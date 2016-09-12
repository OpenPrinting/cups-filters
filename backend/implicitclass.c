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

/* IPP Attribute which cups-browsed uses to tell us the destination queue for
   the current job */
#define CUPS_BROWSED_DEST_PRINTER "cups-browsed-dest-printer"

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
  char queue_name[1024];
  const char *ptr1;
  char *ptr2;
  const char *job_id;
  int i;
  char dest_host[1024];	/* Destination host */
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  char uri[HTTP_MAX_URI];
  static const char *pattrs[] =
                {
                  "printer-defaults"
                };
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
      fprintf(stderr, "ERROR: Incorrect device URI syntax: %s\n",
	      device_uri);
      return (CUPS_BACKEND_STOP);
    }
    ptr1 ++;
    strncpy(queue_name, ptr1, sizeof(queue_name));
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		     "localhost", ippPort(), "/printers/%s", queue_name);
    job_id = argv[1];
    for (i = 0; i < 40; i++) {
      /* Wait up to 20 sec for cups-browsed to supply the destination host */
      /* Try reading the option in which cups-browsed has deposited the
	 destination host */
      request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
		   uri);
      ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		    "requested-attributes",
		    sizeof(pattrs) / sizeof(pattrs[0]),
		    NULL, pattrs);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		   "requesting-user-name",
		   NULL, cupsUser());
      if ((response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/")) ==
	  NULL)
	goto failed;
      for (attr = ippFirstAttribute(response); attr != NULL;
	   attr = ippNextAttribute(response)) {
	while (attr != NULL && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
	  attr = ippNextAttribute(response);
	if (attr == NULL)
	  break;
	ptr1 = NULL;
	while (attr != NULL && ippGetGroupTag(attr) ==
	       IPP_TAG_PRINTER) {
	  if (!strcmp(ippGetName(attr),
		      CUPS_BROWSED_DEST_PRINTER "-default"))
	    ptr1 = ippGetString(attr, 0, NULL);
	  if (ptr1 != NULL)
	    break;
	  attr = ippNextAttribute(response);
	}
	if (ptr1 != NULL)
	  break;
      }
      fprintf(stderr, "DEBUG: Read " CUPS_BROWSED_DEST_PRINTER " option: %s\n", ptr1);
      if (ptr1 == NULL)
	goto failed;
      /* Destination host is between double quotes, as double quotes are
	 illegal in host names one easily recognizes whether the option is
	 complete and avoids accepting a partially written host name */
      if (*ptr1 != '"')
	goto failed;
      ptr1 ++;
      /* Check whether option was set for this job, if not, keep waiting */
      if (strncmp(ptr1, job_id, strlen(job_id)) != 0)
	goto failed;
      ptr1 += strlen(job_id);
      if (*ptr1 != ' ')
	goto failed;
      ptr1 ++;
      /* Read destination host name (or message) and check whether it is
	 complete (second double quote) */
      strncpy(dest_host, ptr1, sizeof(dest_host));
      ptr1 = dest_host;
      if ((ptr2 = strchr(ptr1, '"')) != NULL) {
	*ptr2 = '\0';
	ippDelete(response);
	break;
      }
    failed:
      ippDelete(response);
      /* Pause half a second before next attempt */
      usleep(500000);
    }

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
      /* Instead of feeding the job into the IPP backend, we re-print it into
	 the server's CUPS queue. This way the job gets spooled on the server
	 and we are not blocked until the job is printed. So a subsequent job
	 will be immediately processed and sent out to another server */
      /* Set destination server */
      snprintf(server_str, sizeof(server_str), "%s", dest_host);
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
	  return (CUPS_BACKEND_RETRY);
	}

	if (cupsFinishDocument(CUPS_HTTP_DEFAULT, queue_name) != IPP_OK) {
	  fprintf(stderr, "ERROR: %s: Unable to complete the job - %s. Retrying.",
		  argv[0], cupsLastErrorString());
	  cupsCancelJob2(CUPS_HTTP_DEFAULT, queue_name, job_id, 0);
	  return (CUPS_BACKEND_RETRY);
	}
      }

      if (job_id < 1) {
	fprintf(stderr, "ERROR: %s: Unable to create job - %s. Retrying.",
		argv[0], cupsLastErrorString());
	return (CUPS_BACKEND_RETRY);
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

