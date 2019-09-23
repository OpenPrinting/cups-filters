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

#include "backend-private.h"
#include <cups/array.h>
#include <ctype.h>
#include <cups/array.h>
#include <ctype.h>
#include <cups/cups.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cupsfilters/pdftoippprinter.h>

/*
 * Local globals...
 */

/* IPP Attribute which cups-browsed uses to tell us the destination queue for
   the current job */
#define CUPS_BROWSED_DEST_PRINTER "cups-browsed-dest-printer"

static int		job_canceled = 0; /* Set to 1 on SIGTERM */

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
 * Set an option in a string of options
 */

void          /* O - 0 on success, 1 on error */
set_option_in_str(char *buf,          /* I - Buffer with option list string */
		  int buflen,         /* I - Length of buffer */
		  const char *option, /* I - Option to change/add */
		  const char *value)  /* I - New value for option, NULL
					     removes option */
{
  char *p1, *p2, *p3;

  if (!buf || buflen == 0 || !option)
    return;
  /* Remove any occurrence of option in the string */
  p1 = buf;
  while (*p1 != '\0' && (p2 = strcasestr(p1, option)) != NULL) {
    if (p2 > buf && *(p2 - 1) != ' ' && *(p2 - 1) != '\t') {
      p1 = p2 + 1;
      continue;
    }
    p1 = p2 + strlen(option);
    if (*p1 != '=' && *p1 != ' ' && *p1 != '\t' && *p1 != '\0')
      continue;
    if(!strcmp(option,"cups-browsed-dest-printer")){
      fprintf(stderr, "DEBUG: Removing cups-browsed-dest-printer option from arguments");
      p3 = strchr(p1, '"');
      p3++;
      p1 = strchr(p3, '"');
    }
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
  char    *filename,    /* PDF file to convert */
           tempfile[1024],
           tempfile_filter[1024];   /* Temporary file */
  int i;
  char dest_host[1024];	/* Destination host */
  ipp_t *request, *response;
  ipp_attribute_t *attr;
  int     bytes;      /* Bytes copied */
  char uri[HTTP_MAX_URI];
  char    *argv_nt[7];
  int     outbuflen,filefd,exit_status,dup_status;
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
      fprintf(stderr, "DEBUG: Read " CUPS_BROWSED_DEST_PRINTER " option: %s\n",
	      ptr1);
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

    if (i >= 40) {
      /* Timeout, no useful data from cups-browsed received */
      fprintf(stderr, "ERROR: No destination host name supplied by cups-browsed for printer \"%s\", is cups-browsed running?\n",
	      queue_name);
      return (CUPS_BACKEND_STOP);
    }
    strncpy(dest_host,ptr1,sizeof(dest_host));
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
      int fd;
      char buffer[8192];
      
      fprintf(stderr, "DEBUG: Received destination host name from cups-browsed: printer-uri %s\n",
	      ptr1);

      /* Parse the command line options and prepare them for the new print
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

      /* Finding the document format in which the pdftoippprinter will
	 convert the pdf file */
      if ((ptr3 = strchr(ptr1, ' ')) != NULL) {
	*ptr3='\0';
	ptr3++;
      }

      /* Finding the resolution requested for the job*/
      if ((ptr4 = strchr(ptr3, ' ')) != NULL) {
	*ptr4='\0';
	ptr4++;
      }

      strncpy(printer_uri, ptr1, sizeof(printer_uri));
      strncpy(document_format, ptr3, sizeof(document_format));
      strncpy(resolution, ptr4, sizeof(resolution));

      fprintf(stderr,"DEBUG: Received job for the printer with the destination uri - %s, Final-document format for the printer - %s and requested resolution - %s\n",
	      printer_uri, document_format, resolution);

      /* We need to send modified arguments to the IPP backend */
      if (argc == 6) {
	/* Copy stdin to a temp file...*/
	if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0){
	  fprintf(stderr,"Debug: Can't Read PDF file.\n");
	  return CUPS_BACKEND_FAILED;
	}
	fprintf(stderr, "Debug: implicitclass - copying to temp print file \"%s\"\n",
		tempfile);
	while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
	  bytes = write(fd, buffer, bytes);
	close(fd);
	filename = tempfile;
      } else{
	/** Use the filename on the command-line...*/
	filename    = argv[6];
	tempfile[0] = '\0';
      }

      /* Copying the argument to a new char** which will be sent to the filter
	 and the ipp backend*/
      for (i = 0; i < 5; i++)
	argv_nt[i] = argv[i];

      /* Few new options will be added to the argv[5]*/
      outbuflen = strlen(argv[5]) + 256;
      argv_nt[5] = calloc(outbuflen, sizeof(char));
      strcpy(argv_nt[5], (const char*)argv[5]);

      /* Filter pdftoippprinter.c will read the input from this file*/
      argv_nt[6] = filename;
      set_option_in_str(argv_nt[5], outbuflen, "output-format",
			document_format);
      set_option_in_str(argv_nt[5], outbuflen, "Resolution",resolution);
      set_option_in_str(argv_nt[5], outbuflen, "cups-browsed-dest-printer",NULL);
      setenv("DEVICE_URI",printer_uri, 1);

      filefd = cupsTempFd(tempfile_filter, sizeof(tempfile_filter));

      /* The output of the last filter in pdftoippprinter will be
         written to this file. We could have sent the output directly
         to the backend, but having this temperory file will help us
         find whether the filter worked correctly and what was the
         document-format of the filtered output.*/
      close(1);
      dup_status = dup(filefd);
      if(dup_status < 0) {
        fprintf(stderr, "Could not write the output of pdftoippprinter printer to tmp file\n");
        return CUPS_BACKEND_FAILED;
      }

      /* Calling pdftoippprinter.c filter*/
      apply_filters(7,argv_nt);

      close(filefd);

      /* We will send the filtered output of the pdftoippprinter.c to
	 the IPP Backend*/
      argv_nt[6] = tempfile_filter;
      fprintf(stderr, "DEBUG: The filtered output of pdftoippprinter is written to file %s\n",
	      tempfile_filter);

      /* Setting the final content type to the best pdl supported by
	 the printer.*/
      if(!strcmp(document_format,"pdf"))
	setenv("FINAL_CONTENT_TYPE", "application/pdf", 1);
      else if(!strcmp(document_format,"raster"))
	setenv("FINAL_CONTENT_TYPE", "image/pwg-raster", 1);
      else if(!strcmp(document_format,"apple-raster"))
	setenv("FINAL_CONTENT_TYPE", "image/urf", 1);
      else if(!strcmp(document_format,"pclm"))
	setenv("FINAL_CONTENT_TYPE", "application/PCLm", 1);
      else if(!strcmp(document_format,"pclxl"))
	setenv("FINAL_CONTENT_TYPE", "application/vnd.hp-pclxl", 1);
      else if(!strcmp(document_format,"pcl"))
	setenv("FINAL_CONTENT_TYPE", "application/pcl", 1);

      ippDelete(response);

      /* The implicitclass backend will send the job directly to the
	 ipp backend*/
      pid_t pid = fork();
      if ( pid == 0 ) {
	fprintf(stderr, "DEBUG: Started IPP Backend with pid: %d\n",getpid());
	execv("/usr/lib/cups/backend/ipp",argv_nt);
      } else {
	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
	  exit_status = WEXITSTATUS(status);
	  fprintf(stderr, "DEBUG: The IPP Backend exited with the status %d\n",
		  exit_status);
	}
	return exit_status;
      }
    }
  } else if (argc != 1) {
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
