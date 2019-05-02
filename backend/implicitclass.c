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
#include <cups/array.h>
#include <ctype.h>
#include <cups/cups.h>
#include <sys/types.h>
#include <sys/wait.h>

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

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

#ifndef HAVE_CUPS_1_6
int
ippGetInteger(ipp_attribute_t *attr,
              int             element)
{
  return (attr->values[element].integer);
}
#endif


int                             /* O  - Next delay value */
next_delay(int current,         /* I  - Current delay value or 0 */
           int *previous)       /* IO - Previous delay value */
{
  int next;          /* Next delay value */
  if (current > 0)
  {
    next      = (current + *previous) % 12;
    *previous = next < current ? 0 : current;
  }
  else
  {
    next      = 1;
    *previous = 0;
  }
  return (next);
}

/*
 * Set an option in a string of options
 */

void          /* O - 0 on success, 1 on error */
set_option_in_str(char *buf,    /* I - Buffer with option list string */
      int buflen,   /* I - Length of buffer */
      const char *option, /* I - Option to change/add */
      const char *value)  /* I - New value for option, NULL
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


int                             /* O - Job ID or 0 on error */
create_job_on_printer(
    http_t        *http,        /* I - Connection to server */
    const char    *uri,         /* I - Printer uri */
    const char    *resource,    /* I - Resource of Destination */
    const char    *title,       /* I - Title of job */
    int           num_options,  /* I - Number of options */
    cups_option_t *options)     /* I - Options */
{
    int             job_id = 0;           /* job-id value */
    ipp_t           *request,             /* Get-Printer-Attributes request */
                    *response;            /* Supported attributes */
    ipp_attribute_t *attr;                /* job-id attribute */
    
  if (job_id)
    job_id = 0;

  fprintf(stderr,"Creating job on printer %s\n",uri);

 /* Build a Create-Job request. */
  if ((request = ippNewRequest(IPP_OP_CREATE_JOB)) == NULL)
  {
    fprintf(stderr, "Unable to create job request\n");
    return (0);
  }

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  if (title)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL,
                 title);
  cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
  cupsEncodeOptions2(request, num_options, options, IPP_TAG_JOB);
  cupsEncodeOptions2(request, num_options, options, IPP_TAG_SUBSCRIPTION);

 /* Send the request and get the job-id. */
  response = cupsDoRequest(http, request, resource);

  if ((attr = ippFindAttribute(response, "job-id", IPP_TAG_INTEGER)) != NULL)
  {
    job_id = ippGetInteger(attr,0);
    fprintf(stderr, "Job created with id %d on printer-uri %s\n",job_id,uri);
  }
  ippDelete(response);
  return (job_id);
}


http_status_t                  /* O - HTTP status of request */
start_document(
    http_t     *http,          /* I - Connection to server */
    char       *uri,           /* I - Destination uri */
    char       *resource,      /* I - Resource*/ 
    int        job_id,         /* I - Job ID */
    const char *docname,       /* I - Name of document */
    const char *format,        /* I - MIME type or @code CUPS_FORMAT_foo@ */
    int        last_document)  /* I - 1 for last document in job, 0 otherwise */
{

  ipp_t   *request;            /* Send-Document request */
  http_status_t status;        /* HTTP status */

  fprintf(stderr, "Creating Start Document Request for printer with uri %s\n", uri);
 /* Create a Send-Document request. */
  if ((request = ippNewRequest(IPP_OP_SEND_DOCUMENT)) == NULL)
  {
    return (HTTP_STATUS_ERROR);
  }

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  if (docname)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "document-name",
                 NULL, docname);
  if (format)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE,
                 "document-format", NULL, format);
  ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", (char)last_document);

 /*  Send and delete the request, then return the status. */
  status = cupsSendRequest(http, request, resource, CUPS_LENGTH_VARIABLE);
  ippDelete(request);
  return (status);
}

ipp_status_t                          /* O - Status of document submission */
finish_document(http_t     *http,     /* I - Connection to server */
               const char *resource)  /* I - Destination Resource */
{
  ippDelete(cupsGetResponse(http, resource));
  return (cupsLastError());
}



ipp_status_t                           /* O - IPP status */
cancel_job(http_t     *http,           /* I - Connection to server */
           const char  *uri,           /* I - Uri of printer */
           const char  *resource,      /* I - Resource of printer */
           int        job_id,          /* I - Job ID */
          int        purge)            /* I - 1 to purge, 0 to cancel */
{
  ipp_t   *request;                    /* IPP request */

 /* Range check input. */
  if (job_id < -1 || (!uri && job_id == 0))
  {
    return (0);
  }

 /*
  * Build an IPP_CANCEL_JOB or IPP_PURGE_JOBS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    job-uri or printer-uri + job-id
  *    requesting-user-name
  *    [purge-job] or [purge-jobs]
  */

  request = ippNewRequest(job_id < 0 ? IPP_OP_PURGE_JOBS : IPP_OP_CANCEL_JOB);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                 uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id",
                  job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

  if (purge && job_id >= 0)
    ippAddBoolean(request, IPP_TAG_OPERATION, "purge-job", 1);
  else if (!purge && job_id < 0)
    ippAddBoolean(request, IPP_TAG_OPERATION, "purge-jobs", 0);
 
 /* Do the request. */
  ippDelete(cupsDoRequest(http, request, resource));
  return (cupsLastError());
}

/*
 * 'main()' - Browse for printers.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  const char	*device_uri;            /* URI with which we were called */
  char scheme[64], username[32], queue_name[1024], resource[32],hostname[1024],
       printer_uri[1024];
  int port, status;
  const char *ptr1 = NULL;
  char *ptr2;
  const char *job_id;
  int i;
  char dest_host[1024];	/* Destination host */
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  http_t          *http;
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
    status = httpSeparateURI(HTTP_URI_CODING_ALL, device_uri,
			     scheme, sizeof(scheme),
			     username, sizeof(username),
			     queue_name, sizeof(queue_name),
			     &port,
			     resource, sizeof(resource));
    if (status != HTTP_URI_STATUS_OK &&
	status != HTTP_URI_STATUS_UNKNOWN_SCHEME) {
      fprintf(stderr, "ERROR: Incorrect device URI syntax: %s\n",
	      device_uri);
      return (CUPS_BACKEND_STOP);
    }
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
      const char *title;
      int num_options = 0;
      cups_option_t *options = NULL;
      int fd, job_id;
      char buffer[8192];
      
      fprintf(stderr, "DEBUG: Received destination host name from cups-browsed: printer-uri %s\n",
      ptr1);

      /* Instead of feeding the job into the IPP backend, we re-print it into
	 the server's CUPS queue. This way the job gets spooled on the server
	 and we are not blocked until the job is printed. So a subsequent job
	 will be immediately processed and sent out to another server */

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
  
  strncpy(printer_uri,ptr1,sizeof(printer_uri));
  status = httpSeparateURI(HTTP_URI_CODING_ALL, ptr1, scheme, sizeof(scheme),
           username, sizeof(username), hostname, sizeof(hostname), &port,
           resource, sizeof(resource));

  if(status != 0){
      fprintf(stderr, "httpSeparateURI error, please check printer uri\n");
      return (CUPS_BACKEND_RETRY);    
  }
  if (!port)
    port = 631;   
  http = httpConnect2(hostname, port,NULL,AF_UNSPEC, HTTP_ENCRYPT_IF_REQUESTED,
         1,3000, NULL);

  job_id = create_job_on_printer(http,printer_uri,resource,
          title ? title : "(stdin)",num_options, options);
  
  /* Queue the job directly on the server */
  if (job_id > 0) {
	http_status_t       status;         /* Write status */
	const char          *format;        /* Document format */
	ssize_t             bytes;          /* Bytes read */

	if (cupsGetOption("raw", num_options, options))
	  format = CUPS_FORMAT_RAW;
	else if ((format = cupsGetOption("document-format", num_options,
					 options)) == NULL)
	  format = CUPS_FORMAT_AUTO;
	
  status = start_document(http,printer_uri,resource,job_id, NULL,
           format, 1);

	while (status == HTTP_CONTINUE &&
	       (bytes = read(fd, buffer, sizeof(buffer))) > 0)
	  status = cupsWriteRequestData(http, buffer, (size_t)bytes);

	if (status != HTTP_CONTINUE) {
	  fprintf(stderr, "ERROR: %s: Unable to queue the print data - %s. Retrying.",
		  argv[0], httpStatus(status));
    finish_document(http, resource);
    cancel_job(http,printer_uri,resource,job_id, 0);
	  return (CUPS_BACKEND_RETRY);
	}

	if (finish_document(http,resource) != IPP_OK) {
	  fprintf(stderr, "ERROR: %s: Unable to complete the job - %s. Retrying.",
		  argv[0], cupsLastErrorString());
	  cancel_job(http,printer_uri,resource, job_id, 0);
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

