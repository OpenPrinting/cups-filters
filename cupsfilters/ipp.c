 /***
  This file is part of cups-filters.

  This file is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This file is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <cups/cups.h>
#include <cups/backend.h>
#include <cupsfilters/ipp.h>
#include <cupsfilters/ppdgenerator.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

enum resolve_uri_converter_type	/**** Resolving DNS-SD based URI ****/
{
  CUPS_BACKEND_URI_CONVERTER = -1,
  IPPFIND_BASED_CONVERTER_FOR_PRINT_URI = 0,
  IPPFIND_BASED_CONVERTER_FOR_FAX_URI = 1
};

static int				
convert_to_port(char *a)		
{
  int port = 0;
  for (int i = 0; i<strlen(a); i++)
    port = port*10 + (a[i] - '0');

  return (port);
}

void
log_printf(char *log,
	   const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
  vsnprintf(log + strlen(log),
	    LOGSIZE - strlen(log) - 1,
	    format, arglist);
  log[LOGSIZE - 1] = '\0';
  va_end(arglist);
}

const char *
resolve_uri(const char *raw_uri)
{
  char *pseudo_argv[2];
  const char *uri;
  int fd1, fd2;

  /* Eliminate any output to stderr, to get rid of the CUPS-backend-specific
     output of the cupsBackendDeviceURI() function */
  fd1 = dup(2);
  fd2 = open("/dev/null", O_WRONLY);
  dup2(fd2, 2);
  close(fd2);

  /* Use the URI resolver of libcups to support DNS-SD-service-name-based
     URIs. The function returns the corresponding host-name-based URI */
  pseudo_argv[0] = (char *)raw_uri;
  pseudo_argv[1] = NULL;
  uri = cupsBackendDeviceURI(pseudo_argv);

  /* Re-activate stderr output */
  dup2(fd1, 2);
  close(fd1);

  return uri;
}

#ifdef HAVE_CUPS_1_6
/* Check how the driverless support is provided */
int
check_driverless_support(const char* uri)
{
  int support_status = DRVLESS_CHECKERR;
  ipp_t *response = NULL;

  response = get_printer_attributes3(NULL, uri, NULL, 0, NULL, 0, 1,
				     &support_status);
  if (response != NULL)
    ippDelete(response);

  return support_status;
}

/* Get attributes of a printer specified only by URI */
ipp_t *
get_printer_attributes(const char* raw_uri,
		       const char* const pattrs[],
		       int pattrs_size,
		       const char* const req_attrs[],
		       int req_attrs_size,
		       int debug)
{
  return get_printer_attributes2(NULL, raw_uri, pattrs, pattrs_size,
				 req_attrs, req_attrs_size, debug);
}

/* Get attributes of a printer specified by URI and under a given HTTP
   connection, for example via a domain socket */
ipp_t *
get_printer_attributes2(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug)
{
  return get_printer_attributes3(http_printer, raw_uri, pattrs, pattrs_size,
				 req_attrs, req_attrs_size, debug, NULL);
}

/* Get attributes of a printer specified by URI and under a given HTTP
   connection, for example via a domain socket, and give info about used
   fallbacks */
ipp_t *
get_printer_attributes3(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug,
                        int* driverless_info)
{
  return get_printer_attributes5(http_printer, raw_uri, pattrs, pattrs_size,
				 req_attrs, req_attrs_size, debug,
				 driverless_info, CUPS_BACKEND_URI_CONVERTER);
}

/* Get attributes of a printer specified only by URI and given info about fax-support*/
ipp_t   *get_printer_attributes4(const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int is_fax)
{
  if(is_fax)
    return get_printer_attributes5(NULL, raw_uri, pattrs, pattrs_size,
				   req_attrs, req_attrs_size, debug, NULL,
				   IPPFIND_BASED_CONVERTER_FOR_FAX_URI);
  else
    return get_printer_attributes5(NULL, raw_uri, pattrs, pattrs_size,
				   req_attrs, req_attrs_size, debug, NULL,
				   IPPFIND_BASED_CONVERTER_FOR_PRINT_URI);
}

/* Get attributes of a printer specified by URI and under a given HTTP
   connection, for example via a domain socket, and give info about used
   fallbacks */
ipp_t *
get_printer_attributes5(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug,
			int* driverless_info,
			int resolve_uri_type )
{
  const char *uri;
  int have_http, uri_status, host_port, i = 0, total_attrs = 0, fallback,
    cap = 0;
  char scheme[10], userpass[1024], host_name[1024], resource[1024];
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  char valuebuffer[65536];
  const char *kw;
  ipp_status_t ipp_status;
  /* Default attributes for get-printer-attributes requests to
     obtain complete capability lists of a printer */
  const char * const pattrs_cap_standard[] = {
    "all",
    "media-col-database",
  };
  const char * const pattrs_cap_fallback[] = {
    "all",
  };
  /* Attributes required in the IPP response of a complete printer
     capability list */
  const char * const req_attrs_cap[] = {
    "attributes-charset",
    "attributes-natural-language",
    "charset-configured",
    "charset-supported",
    "compression-supported",
    "document-format-default",
    "document-format-supported",
    "generated-natural-language-supported",
    "ipp-versions-supported",
    "natural-language-configured",
    "operations-supported",
    "printer-is-accepting-jobs",
    "printer-name",
    "printer-state",
    "printer-state-reasons",
    "printer-up-time",
    "printer-uri-supported",
    "uri-authentication-supported",
    "uri-security-supported"
  };

  /* Expect a device capable of standard IPP Everywhere */
  if (driverless_info != NULL)
    *driverless_info = FULL_DRVLESS;

  /* Request printer properties via IPP, for example to
      - generate a PPD file for the printer
        (mainly driverless-capable printers)
      - generally find capabilities, options, and default settinngs,
      - printers status: Accepting jobs? Busy? With how many jobs? */

  get_printer_attributes_log[0] = '\0';

  /* Convert DNS-SD-service-name-based URIs to host-name-based URIs */
  if(resolve_uri_type == CUPS_BACKEND_URI_CONVERTER)
    uri = resolve_uri(raw_uri);
  else
    uri = ippfind_based_uri_converter(raw_uri, resolve_uri_type);

  /* Extract URI componants needed for the IPP request */
  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri,
			       scheme, sizeof(scheme),
			       userpass, sizeof(userpass),
			       host_name, sizeof(host_name),
			       &(host_port),
			       resource, sizeof(resource));
  if (uri_status != HTTP_URI_OK) {
    /* Invalid URI */
    log_printf(get_printer_attributes_log,
	       "get-printer-attributes: Cannot parse the printer URI: %s\n",
	       uri);
    return NULL;
  }

  /* Connect to the server if not already done */
  if (http_printer == NULL) {
    have_http = 0;
    if ((http_printer =
	 httpConnect2 (host_name, host_port, NULL, AF_UNSPEC, 
		       HTTP_ENCRYPT_IF_REQUESTED, 1, 3000, NULL)) == NULL) {
      log_printf(get_printer_attributes_log,
		 "get-printer-attributes: Cannot connect to printer with URI %s.\n",
		 uri);
      return NULL;
    }
  } else
    have_http = 1;

  /* If we got called without attribute list, use the attributes for polling
     a complete list of capabilities of the printer.
     If also no list of required attributes in the response is supplied, use
     the default list */
  if (pattrs == NULL || pattrs_size == 0) {
    cap = 1;
    pattrs = pattrs_cap_standard;
    pattrs_size = sizeof(pattrs_cap_standard) / sizeof(pattrs_cap_standard[0]);
    if (req_attrs == NULL || req_attrs_size == 0) {
      req_attrs = req_attrs_cap;
      req_attrs_size = sizeof(req_attrs_cap) / sizeof(req_attrs_cap[0]);
    }
  }

  /* Loop through all fallbacks until getting a successful result */
  for (fallback = 0; fallback < 2 + cap; fallback ++) {
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    if (fallback == 1)
      /* Fallback 1: Try IPP 1.1 instead of 2.0 */
      ippSetVersion(request, 1, 1);
    if (fallback == 2 && cap) {
      /* Fallback 2: (Only for full capability list) Try only "all",
	 without "media-col-database */
      pattrs = pattrs_cap_fallback;
      pattrs_size = sizeof(pattrs_cap_fallback) /
	sizeof(pattrs_cap_fallback[0]);
    }
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
		 uri);
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		  "requested-attributes", pattrs_size,
		  NULL, pattrs);

    response = cupsDoRequest(http_printer, request, resource);
    ipp_status = cupsLastError();

    if (response) {
      log_printf(get_printer_attributes_log,
		 "Requested IPP attributes (get-printer-attributes) for printer with URI %s\n",
		 uri);
      /* Log all printer attributes for debugging and count them */
      if (debug)
	log_printf(get_printer_attributes_log,
		   "Full list of all IPP attributes:\n");
      attr = ippFirstAttribute(response);
      while (attr) {
	total_attrs ++;
	if (debug) {
	  ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	  log_printf(get_printer_attributes_log,
		     "  Attr: %s\n",ippGetName(attr));
	  log_printf(get_printer_attributes_log,
		     "  Value: %s\n", valuebuffer);
	  for (i = 0; i < ippGetCount(attr); i ++) {
	    if ((kw = ippGetString(attr, i, NULL)) != NULL) {
	      log_printf(get_printer_attributes_log, "  Keyword: %s\n", kw);
	    }
	  }
	}
	attr = ippNextAttribute(response);
      }

      /* Check whether the IPP response contains the required attributes
	 and is not incomplete */
      if (req_attrs)
	for (i = req_attrs_size; i > 0; i --)
	  if (ippFindAttribute(response, req_attrs[i - 1], IPP_TAG_ZERO) ==
	      NULL)
	    break;
      if (ipp_status == IPP_STATUS_ERROR_BAD_REQUEST ||
	  ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED ||
	  (req_attrs && i > 0) || (cap && total_attrs < 20)) {
	log_printf(get_printer_attributes_log,
		   "get-printer-attributes IPP request failed:\n");
	if (ipp_status == IPP_STATUS_ERROR_BAD_REQUEST)
	  log_printf(get_printer_attributes_log,
		     "  - ipp_status == IPP_STATUS_ERROR_BAD_REQUEST\n");
	else if (ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED)
	  log_printf(get_printer_attributes_log,
		     "  - ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED\n");
	if (req_attrs && i > 0)
	  log_printf(get_printer_attributes_log,
		     "  - Required IPP attribute %s not found\n",
		     req_attrs[i - 1]);
	if (cap && total_attrs < 20)
	  log_printf(get_printer_attributes_log,
		     "  - Too few IPP attributes: %d (30 or more expected)\n",
		     total_attrs);
	ippDelete(response);
      } else {
	/* Suitable response, we are done */
	if (have_http == 0) httpClose(http_printer);
	return response;
      }
    } else {
      log_printf(get_printer_attributes_log,
		 "Request for IPP attributes (get-printer-attributes) for printer with URI %s failed: %s\n",
		 uri, cupsLastErrorString());
      log_printf(get_printer_attributes_log, "get-printer-attributes IPP request failed:\n");
      log_printf(get_printer_attributes_log, "  - No response\n");
    }
    if (fallback == 1 + cap) {
      log_printf(get_printer_attributes_log,
		 "No further fallback available, giving up\n");
      if (driverless_info != NULL)
        *driverless_info = DRVLESS_CHECKERR;
    } else if (cap && fallback == 1) {
      log_printf(get_printer_attributes_log,
		 "The server doesn't support the standard IPP request, trying request without media-col\n");
      if (driverless_info != NULL)
        *driverless_info = DRVLESS_INCOMPLETEIPP;
    } else if (fallback == 0) {
      log_printf(get_printer_attributes_log,
		 "The server doesn't support IPP2.0 request, trying IPP1.1 request\n");
      if (driverless_info != NULL)
        *driverless_info = DRVLESS_IPP11;
    }
  }

  if (have_http == 0) httpClose(http_printer);
  return NULL;
}

const char*
ippfind_based_uri_converter (const char *uri, int is_fax)
{
  int  ippfind_pid = 0,	        /* Process ID of ippfind for IPP */
       post_proc_pipe[2],	/* Pipe to post-processing for IPP */
       wait_children,		/* Number of child processes left */
       wait_pid,		/* Process ID from wait() */
       wait_status,		/* Status from child */
       exit_status = 0,		/* Exit status */
       bytes,
       port,
       i,
       output_of_fax_uri = 0,
       is_local;
  char *ippfind_argv[100],	/* Arguments for ippfind */
       *ptr_to_port = NULL,
       *reg_type,
       *resolved_uri,		/*  Buffer for resolved URI */
       *resource_field = NULL,
       *service_hostname = NULL,
       /* URI components... */
       scheme[32],
       userpass[256],
       hostname[1024],
       resource[1024],
       buffer[8192],		/* Copy buffer */
       *ptr;			/* Pointer into string */;
  cups_file_t *fp;		/* Post-processing input file */
  int  status;			/* Status of GET request */

  resolved_uri = (char *)malloc(2048 * (sizeof(char)));

  status = httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme),
			   userpass, sizeof(userpass),
			   hostname, sizeof(hostname), &port, resource,
			   sizeof(resource));
  if (status < HTTP_URI_OK) {
    /* Invalid URI */
    fprintf(stderr, "ERROR: Could not parse URI: %s\n", uri);
    goto error;
  }

  /* URI is not DNS-SD-based, so do not resolve */
  if ((reg_type = strstr(hostname, "._tcp")) == NULL) {
    free(resolved_uri);
    return strdup(uri);
  }

  reg_type --;
  while (reg_type >= hostname && *reg_type != '.')
    reg_type --;
  if (reg_type < hostname) {
    fprintf(stderr, "ERROR: Invalid DNS-SD service name: %s\n", hostname);
    goto error;
  }
  *reg_type++ = '\0';

  i = 0;
  ippfind_argv[i++] = "ippfind";
  ippfind_argv[i++] = reg_type;           /* list IPP(S) entries */
  ippfind_argv[i++] = "-T";               /* DNS-SD poll timeout */
  ippfind_argv[i++] = "0";                /* Minimum time required */
  if (is_fax) {
    ippfind_argv[i++] = "--txt";
    ippfind_argv[i++] = "rfo";
  }
  ippfind_argv[i++] = "-N";
  ippfind_argv[i++] = hostname;
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             /* Output the needed data fields */
  ippfind_argv[i++] = "-en";              /* separated by tab characters */
  if(is_fax)
    ippfind_argv[i++] = "\n{service_hostname}\t{txt_rfo}\t{service_port}\t";
  else
    ippfind_argv[i++] = "\n{service_hostname}\t{txt_rp}\t{service_port}\t";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = "--local";          /* Rest only if local service */
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             /* Output an 'L' at the end of the */
  ippfind_argv[i++] = "-en";              /* line */
  ippfind_argv[i++] = "L";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = NULL;

 /*
  * Create a pipe for passing the ippfind output to post-processing
  */

  if (pipe(post_proc_pipe)) {
    perror("ERROR: Unable to create pipe to post-processing");
    goto error;
  }

  if ((ippfind_pid = fork()) == 0) {
   /*
    * Child comes here...
    */

    dup2(post_proc_pipe[1], 1);
    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);

    execvp(CUPS_IPPFIND, ippfind_argv);
    perror("ERROR: Unable to execute ippfind utility");

    exit(1);
  }
  else if (ippfind_pid < 0) {
   /*
    * Unable to fork!
    */

    perror("ERROR: Unable to execute ippfind utility");
    goto error;
  }

  dup2(post_proc_pipe[0], 0);
  close(post_proc_pipe[0]);
  close(post_proc_pipe[1]);

  fp = cupsFileStdin();

  while ((bytes = cupsFileGetLine(fp, buffer, sizeof(buffer))) > 0) {
    /* Mark all the fields of the output of ippfind */
    ptr = buffer;
    /* First, build the DNS-SD-service-name-based URI ... */
    while (ptr && !isalnum(*ptr & 255)) ptr ++;

    service_hostname = ptr; 
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    resource_field = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    ptr_to_port = ptr;
    ptr = memchr(ptr, '\t', sizeof(buffer) - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    /* Do we have a local service so that we have to set the host name to
       "localhost"? */
    is_local = (*ptr == 'L');

    ptr = strchr(reg_type, '.');
    if (!ptr) goto read_error;
    *ptr = '\0';

    port = convert_to_port(ptr_to_port);

    httpAssembleURIf(HTTP_URI_CODING_ALL, resolved_uri,
		     2047, reg_type + 1, NULL,
		     (is_local ? "localhost" : service_hostname), port, "/%s",
		     resource_field);

    if (is_fax)
      output_of_fax_uri = 1; /* fax-uri requested from fax-capable device */

  read_error:
    continue;
  }

 /*
  * Wait for the child processes to exit...
  */

  wait_children = 1;

  while (wait_children > 0) {
   /*
    * Wait until we get a valid process ID or the job is canceled...
    */

    while ((wait_pid = wait(&wait_status)) < 0 && errno == EINTR) {
    }

    if (wait_pid < 0)
      break;

    wait_children --;

   /*
    * Report child status...
    */

    if (wait_status) {
      if (WIFEXITED(wait_status)) {
	exit_status = WEXITSTATUS(wait_status);
        if (wait_pid == ippfind_pid && exit_status <= 2)
          exit_status = 0;	  
      } else if (WTERMSIG(wait_status) == SIGTERM) {
      } else {
	exit_status = WTERMSIG(wait_status);
      }
    }
  }
  if (is_fax && !output_of_fax_uri) {
    fprintf(stderr, "fax URI requested from not fax-capable device\n");
    goto error;
  }

  return (resolved_uri);

 /*
  * Exit...
  */

 error:
  return (NULL);
}


#endif /* HAVE_CUPS_1_6 */
