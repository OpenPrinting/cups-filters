/*
 * implicitclass backend for implementing an implicit-class-like behavior
 * of redundant print servers managed by cups-browsed.
 *
 * Copyright 2015-2019 by Till Kamppeter
 * Copyright 2018-2019 by Deepak Patankar
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

#include <config.h>
#include <cups/cups.h>
#include <cups/backend.h>
#include <cups/array.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cupsfilters/filter.h>
#include <cupsfilters/ipp.h>

/*
 * Local globals...
 */

/* IPP Attribute which cups-browsed uses to tell us the destination queue for
   the current job */
#define CUPS_BROWSED_DEST_PRINTER "cups-browsed-dest-printer"

static int		job_canceled = 0; /* Set to 1 on SIGTERM */

/*
 * Local functions...
 */

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
  if (current > 0) {
    next      = (current + *previous) % 12;
    *previous = next < current ? 0 : current;
  } else {
    next      = 1;
    *previous = 0;
  }
  return (next);
}

/*
 * 'main()' - Browse for printers.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  const char	*device_uri;            /* URI with which we were called */
  char scheme[64], username[32], queue_name[1024], resource[32],
       printer_uri[1024],document_format[256],resolution[16];
  int port, status;
  const char *ptr1 = NULL;
  char *ptr2,*ptr3,*ptr4;
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

  if (argc >= 6) {
    if ((device_uri = getenv("DEVICE_URI")) == NULL) {
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
    for (i = 0; i < 120; i++) {
      /* Wait up to 60 sec for cups-browsed to supply the destination host */
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
      fprintf(stderr, "DEBUG: Read " CUPS_BROWSED_DEST_PRINTER " option: %s\n",
	      (ptr1 ? ptr1 : "Option not found"));
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
	break;
      }
    failed:
      /* Pause half a second before next attempt */
      usleep(500000);
    }

    if (i >= 120) {
      /* Timeout, no useful data from cups-browsed received */
      fprintf(stderr, "ERROR: No destination host name supplied by cups-browsed for printer \"%s\", is cups-browsed running?\n",
	      queue_name);
      return (CUPS_BACKEND_STOP);
    }
    strncpy(dest_host,ptr1,sizeof(dest_host) - 1);
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
      char *title;
      int num_options = 0;
      cups_option_t *options = NULL;
      int fd, nullfd;
      cf_filter_data_t filter_data;
      cf_filter_universal_parameter_t universal_parameters;
      cf_filter_external_cups_t ipp_backend_params;
      cf_filter_filter_in_chain_t universal_in_chain,
	                       ipp_in_chain;
      cups_array_t *filter_chain;
      int retval;

      fprintf(stderr, "DEBUG: Received destination host name from cups-browsed: printer-uri %s\n",
	      ptr1);

      /* Parse the command line options and prepare them for the new print
	 job */
      cupsSetUser(argv[2]);
      title = argv[3];
      if (title == NULL) {
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

      /* Finding the document format in which the pdftoippprinter will
	 convert the pdf file */
      if ((ptr3 = strchr(ptr1, ' ')) != NULL) {
	*ptr3='\0';
	ptr3++;
      }

      /* Finding the resolution requested for the job */
      if ((ptr4 = strchr(ptr3, ' ')) != NULL) {
	*ptr4='\0';
	ptr4++;
      }

      strncpy(printer_uri, ptr1, sizeof(printer_uri) - 1);
      strncpy(document_format, ptr3, sizeof(document_format) - 1);
      strncpy(resolution, ptr4, sizeof(resolution) - 1);

      fprintf(stderr,"DEBUG: Received job for the printer with the destination uri - %s, Final-document format for the printer - %s and requested resolution - %s\n",
	      printer_uri, document_format, resolution);

      /* Adjust option list for the cfFilterUniversal() filter function call */
      num_options = cupsAddOption("Resolution", resolution,
				  num_options, &options);
      num_options = cupsRemoveOption("cups-browsed-dest-printer",
				     num_options, &options);
      num_options = cupsRemoveOption("cups-browsed",
				     num_options, &options);

      /* Set up filter data record to be used by the filter functions to
	 process the job */
      filter_data.printer = printer_uri;
      filter_data.job_id = atoi(argv[1]);
      filter_data.job_user = argv[2];
      filter_data.job_title = title;
      filter_data.copies = atoi(argv[4]);
      filter_data.job_attrs = NULL;        /* We use command line options */
      if ((filter_data.printer_attrs =
	   cfGetPrinterAttributes4(printer_uri, NULL, 0, NULL, 0, 1, 0)) !=
	  NULL)
                                           /* Poll the printer attributes from
					      the printer */
	filter_data.ppdfile = NULL;        /* We have successfully polled
					      the IPP attributes from the
					      printer. This is the most
					      precise printer capability info.
					      As the queue's PPD is only
					      for the cluster we prefer the
					      IPP attributes */
      else
	filter_data.ppdfile = getenv("PPD");/*The polling of the printer's
					      IPP attribute failed, meaning
					      that it is most probably not a
					      driverless IPP printers (IPP 2.x)
					      but a legacy IPP printer (IPP
					      1.x) which usually has
					      unsufficient capability info.
					      Therefore we fall back to the
					      PPD file here which contains
					      some info from the printer's
					      DNS-SD record. */
      filter_data.num_options = num_options;
      filter_data.options = options;       /* Command line options from 5th
					      arg */
      filter_data.back_pipe[0] = -1;
      filter_data.back_pipe[1] = -1;
      filter_data.side_pipe[0] = -1;
      filter_data.side_pipe[1] = -1;
      filter_data.logfunc = cfCUPSLogFunc;  /* Logging scheme of CUPS */
      filter_data.logdata = NULL;
      filter_data.iscanceledfunc = cfCUPSIsCanceledFunc; /* Job-is-canceled
							   function */
      filter_data.iscanceleddata = &job_canceled;

      cfFilterLoadPPD(&filter_data);
      if (filter_data.printer_attrs == NULL && filter_data.ppd == NULL)
      {
	ippDelete(response);
	fprintf(stderr, "ERROR: Unable to get sufficient capability info of the destination printer.\n");
	return (CUPS_BACKEND_FAILED);
      }

      cfFilterOpenBackAndSidePipes(&filter_data);

      /* Parameters (input/output MIME types) for cfFilterUniversal() call */
      universal_parameters.input_format = "application/vnd.cups-pdf";
      universal_parameters.output_format = document_format;
      memset(&(universal_parameters.texttopdf_params), 0,
	     sizeof(cf_filter_texttopdf_parameter_t));

      /* Parameters for cfFilterExternalCUPS() call for IPP backend */
      ipp_backend_params.filter = "ipp";
      ipp_backend_params.is_backend = 1;
      ipp_backend_params.device_uri = printer_uri;
      ipp_backend_params.num_options = 0;
      ipp_backend_params.options = NULL;
      ipp_backend_params.envp = NULL;

      /* Filter chain entry for the cfFilterUniversal() filter function call */
      universal_in_chain.function = cfFilterUniversal;
      universal_in_chain.parameters = &universal_parameters;
      universal_in_chain.name = "Filters";

      /* Filter chain entry for the IPP CUPS backend call */
      ipp_in_chain.function = cfFilterExternalCUPS;
      ipp_in_chain.parameters = &ipp_backend_params;
      ipp_in_chain.name = "Backend";

      filter_chain = cupsArrayNew(NULL, NULL);
      cupsArrayAdd(filter_chain, &universal_in_chain);
      cupsArrayAdd(filter_chain, &ipp_in_chain);

      /* DEVICE_URI environment variable */
      setenv("DEVICE_URI", printer_uri, 1);

      /* FINAL_CONTENT_TYPE environment variable */
      setenv("FINAL_CONTENT_TYPE", document_format, 1);

      /* We call the IPP CUPS backend at the end of the chain, so we have
	 no output */
      nullfd = open("/dev/null", O_WRONLY);

      /* Call the filter chain to run the needed filters and the backend */
      retval = cfFilterChain(fd, nullfd, fd != 0 ? 1 : 0, &filter_data,
			   filter_chain);

      cfFilterCloseBackAndSidePipes(&filter_data);

      /* Clean up */
      cupsArrayDelete(filter_chain);
      ippDelete(response);
      cfFilterFreePPD(&filter_data);

      if (retval) {
	fprintf(stderr, "ERROR: Job processing failed.\n");
	return (CUPS_BACKEND_FAILED);
      }
    }
  } else if (argc != 1) {
    fprintf(stderr,
	    "Usage: %s job-id user title copies options [file]\n",
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
