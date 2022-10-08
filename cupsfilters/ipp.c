//
//  IPP-related functions for libcupsfilters.
//
//  This file is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as
//  published by the Free Software Foundation; either version 2.1 of the
//  License, or (at your option) any later version.
//
//  This file is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
//  Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with avahi; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
//  USA.
//
// Contents:
//
//   cfGetBackSideOrientation() - Return backside orientation for duplex
//                                printing
//   cfGetPrintRenderIntent()   - Return rendering intent for a job
//   cfJoinJobOptionsAndAttrs() - Join job IPP attributes and job options in
//                                one option list
//

//
// Include necessary headers.
//

#include <config.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cups/cups.h>
#include <cups/backend.h>
#include <cups/dir.h>
#include <cups/pwg.h>
#include <cupsfilters/ipp.h>


enum resolve_uri_converter_type	// **** Resolving DNS-SD based URI ****
{
  CUPS_BACKEND_URI_CONVERTER = -1,
  IPPFIND_BASED_CONVERTER_FOR_PRINT_URI = 0,
  IPPFIND_BASED_CONVERTER_FOR_FAX_URI = 1
};

char cf_get_printer_attributes_log[CF_GET_PRINTER_ATTRIBUTES_LOGSIZE];

static int				
convert_to_port(char *a)		
{
  int port = 0;
  for (int i = 0; i < strlen(a); i ++)
    port = port*10 + (a[i] - '0');

  return (port);
}

static void
log_printf(char *log,
	   const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
  vsnprintf(log + strlen(log),
	    CF_GET_PRINTER_ATTRIBUTES_LOGSIZE - strlen(log) - 1,
	    format, arglist);
  log[CF_GET_PRINTER_ATTRIBUTES_LOGSIZE - 1] = '\0';
  va_end(arglist);
}

char *
cfResolveURI(const char *raw_uri)
{
  char *pseudo_argv[2];
  const char *uri;
  int fd1, fd2;
  char *save_device_uri_var;

  // Eliminate any output to stderr, to get rid of the CUPS-backend-specific
  // output of the cupsBackendDeviceURI() function
  fd1 = dup(2);
  fd2 = open("/dev/null", O_WRONLY);
  dup2(fd2, 2);
  close(fd2);

  // If set, save the DEVICE_URI environment and then unset it, so that
  // if we are running under CUPS (as filter or backend) our raw_uri gets
  // resolved and not whatever URI is set in DEVICE_URI
  if ((save_device_uri_var = getenv("DEVICE_URI")) != NULL)
  {
    save_device_uri_var = strdup(save_device_uri_var);
    unsetenv("DEVICE_URI");
  }

  // Use the URI resolver of libcups to support DNS-SD-service-name-based
  // URIs. The function returns the corresponding host-name-based URI
  pseudo_argv[0] = (char *)raw_uri;
  pseudo_argv[1] = NULL;
  uri = cupsBackendDeviceURI(pseudo_argv);

  // Restore DEVICE_URI environment variable if we had unset it
  if (save_device_uri_var)
  {
    setenv("DEVICE_URI", save_device_uri_var, 1);
    free(save_device_uri_var);
  }

  // Re-activate stderr output
  dup2(fd1, 2);
  close(fd1);

  return (uri ? strdup(uri) : NULL);
}

// Check how the driverless support is provided
int
cfCheckDriverlessSupport(const char* uri)
{
  int support_status = CF_DRVLESS_CHECKERR;
  ipp_t *response = NULL;

  response = cfGetPrinterAttributes3(NULL, uri, NULL, 0, NULL, 0, 1,
				     &support_status);
  if (response != NULL)
    ippDelete(response);

  return (support_status);
}

// Get attributes of a printer specified only by URI
ipp_t *
cfGetPrinterAttributes(const char* raw_uri,
		       const char* const pattrs[],
		       int pattrs_size,
		       const char* const req_attrs[],
		       int req_attrs_size,
		       int debug)
{
  return (cfGetPrinterAttributes2(NULL, raw_uri, pattrs, pattrs_size,
				  req_attrs, req_attrs_size, debug));
}

// Get attributes of a printer specified by URI and under a given HTTP
// connection, for example via a domain socket
ipp_t *
cfGetPrinterAttributes2(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug)
{
  return (cfGetPrinterAttributes3(http_printer, raw_uri, pattrs, pattrs_size,
				  req_attrs, req_attrs_size, debug, NULL));
}

// Get attributes of a printer specified by URI and under a given HTTP
// connection, for example via a domain socket, and give info about used
// fallbacks
ipp_t *
cfGetPrinterAttributes3(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug,
                        int* driverless_info)
{
  return (cfGetPrinterAttributes5(http_printer, raw_uri, pattrs, pattrs_size,
				  req_attrs, req_attrs_size, debug,
				  driverless_info, CUPS_BACKEND_URI_CONVERTER));
}

// Get attributes of a printer specified only by URI and given info about
// fax-support
ipp_t   *cfGetPrinterAttributes4(const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int is_fax)
{
  if (is_fax)
    return (cfGetPrinterAttributes5(NULL, raw_uri, pattrs, pattrs_size,
				    req_attrs, req_attrs_size, debug, NULL,
				    IPPFIND_BASED_CONVERTER_FOR_FAX_URI));
  else
    return (cfGetPrinterAttributes5(NULL, raw_uri, pattrs, pattrs_size,
				    req_attrs, req_attrs_size, debug, NULL,
				    IPPFIND_BASED_CONVERTER_FOR_PRINT_URI));
}

// Get attributes of a printer specified by URI and under a given HTTP
// connection, for example via a domain socket, and give info about used
// fallbacks
ipp_t *
cfGetPrinterAttributes5(http_t *http_printer,
			const char* raw_uri,
			const char* const pattrs[],
			int pattrs_size,
			const char* const req_attrs[],
			int req_attrs_size,
			int debug,
			int* driverless_info,
			int resolve_uri_type )
{
  char *uri;
  int have_http, uri_status, host_port, i = 0, total_attrs = 0, fallback,
    cap = 0;
  char scheme[10], userpass[1024], host_name[1024], resource[1024];
  http_encryption_t encryption;
  ipp_t *request, *response = NULL;
  ipp_attribute_t *attr;
  char valuebuffer[65536];
  const char *kw;
  ipp_status_t ipp_status;
  // Default attributes for get-printer-attributes requests to
  // obtain complete capability lists of a printer
  const char * const pattrs_cap_standard[] =
  {
    "all",
    "media-col-database",
  };
  const char * const pattrs_cap_fallback[] =
  {
    "all",
  };
  // Attributes required in the IPP response of a complete printer
  // capability list
  const char * const req_attrs_cap[] =
  {
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

  // Expect a device capable of standard IPP Everywhere
  if (driverless_info != NULL)
    *driverless_info = CF_DRVLESS_FULL;

  //
  // Request printer properties via IPP, for example to
  //  - Find capabilities, options, and default settings
  //  - Printer's status: Accepting jobs? Busy? With how many jobs?
  //  - Generate a PPD file for the printer
  //    (mainly driverless-capable printers with CUPS 2.x)
  //

  cf_get_printer_attributes_log[0] = '\0';

  // Convert DNS-SD-service-name-based URIs to host-name-based URIs
  if (resolve_uri_type == CUPS_BACKEND_URI_CONVERTER)
    uri = cfResolveURI(raw_uri);
  else
    uri = cfippfindBasedURIConverter(raw_uri, resolve_uri_type);

  if (uri == NULL)
  {
    log_printf(cf_get_printer_attributes_log,
        "get-printer-attibutes: Cannot resolve URI: %s\n", raw_uri);
    return NULL;
  }

  // Extract URI componants needed for the IPP request
  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri,
			       scheme, sizeof(scheme),
			       userpass, sizeof(userpass),
			       host_name, sizeof(host_name),
			       &(host_port),
			       resource, sizeof(resource));
  if (uri_status != HTTP_URI_OK)
  {
    // Invalid URI
    log_printf(cf_get_printer_attributes_log,
	       "get-printer-attributes: Cannot parse the printer URI: %s\n",
	       uri);
    if (uri) free(uri);
    return NULL;
  }

  if (!strcmp(scheme, "ipps"))
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  // Connect to the server if not already done
  if (http_printer == NULL)
  {
    have_http = 0;
    if ((http_printer =
	 httpConnect2 (host_name, host_port, NULL, AF_UNSPEC, 
		       encryption, 1, 3000, NULL)) == NULL)
    {
      log_printf(cf_get_printer_attributes_log,
		 "get-printer-attributes: Cannot connect to printer with URI %s.\n",
		 uri);
      if (uri) free(uri);
      return NULL;
    }
  }
  else
    have_http = 1;

  // If we got called without attribute list, use the attributes for polling
  // a complete list of capabilities of the printer.
  // If also no list of required attributes in the response is supplied, use
  // the default list
  if (pattrs == NULL || pattrs_size == 0)
  {
    cap = 1;
    pattrs = pattrs_cap_standard;
    pattrs_size = sizeof(pattrs_cap_standard) / sizeof(pattrs_cap_standard[0]);
    if (req_attrs == NULL || req_attrs_size == 0)
    {
      req_attrs = req_attrs_cap;
      req_attrs_size = sizeof(req_attrs_cap) / sizeof(req_attrs_cap[0]);
    }
  }

  // Loop through all fallbacks until getting a successful result
  for (fallback = 0; fallback < 2 + cap; fallback ++)
  {
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    if (fallback == 1)
      // Fallback 1: Try IPP 1.1 instead of 2.0
      ippSetVersion(request, 1, 1);
    if (fallback == 2 && cap)
    {
      // Fallback 2: (Only for full capability list) Try only "all",
      // without "media-col-database"
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

    if (response)
    {
      log_printf(cf_get_printer_attributes_log,
		 "Requested IPP attributes (get-printer-attributes) for printer with URI %s\n",
		 uri);
      // Log all printer attributes for debugging and count them
      if (debug)
	log_printf(cf_get_printer_attributes_log,
		   "Full list of all IPP attributes:\n");
      attr = ippFirstAttribute(response);
      while (attr)
      {
	total_attrs ++;
	if (debug)
	{
	  ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
	  log_printf(cf_get_printer_attributes_log,
		     "  Attr: %s\n",ippGetName(attr));
	  log_printf(cf_get_printer_attributes_log,
		     "  Value: %s\n", valuebuffer);
	  for (i = 0; i < ippGetCount(attr); i ++)
	    if ((kw = ippGetString(attr, i, NULL)) != NULL)
	      log_printf(cf_get_printer_attributes_log, "  Keyword: %s\n", kw);
	}
	attr = ippNextAttribute(response);
      }

      // Check whether the IPP response contains the required attributes
      // and is not incomplete
      if (req_attrs)
	for (i = req_attrs_size; i > 0; i --)
	  if (ippFindAttribute(response, req_attrs[i - 1], IPP_TAG_ZERO) ==
	      NULL)
	    break;
      if (ipp_status == IPP_STATUS_ERROR_BAD_REQUEST ||
	  ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED ||
	  (req_attrs && i > 0) || (cap && total_attrs < 20))
      {
	log_printf(cf_get_printer_attributes_log,
		   "get-printer-attributes IPP request failed:\n");
	if (ipp_status == IPP_STATUS_ERROR_BAD_REQUEST)
	  log_printf(cf_get_printer_attributes_log,
		     "  - ipp_status == IPP_STATUS_ERROR_BAD_REQUEST\n");
	else if (ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED)
	  log_printf(cf_get_printer_attributes_log,
		     "  - ipp_status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED\n");
	if (req_attrs && i > 0)
	  log_printf(cf_get_printer_attributes_log,
		     "  - Required IPP attribute %s not found\n",
		     req_attrs[i - 1]);
	if (cap && total_attrs < 20)
	  log_printf(cf_get_printer_attributes_log,
		     "  - Too few IPP attributes: %d (30 or more expected)\n",
		     total_attrs);
	ippDelete(response);
      }
      else
      {
	// Suitable response, we are done
	if (have_http == 0) httpClose(http_printer);
	if (uri) free(uri);
	return response;
      }
    }
    else
    {
      log_printf(cf_get_printer_attributes_log,
		 "Request for IPP attributes (get-printer-attributes) for printer with URI %s failed: %s\n",
		 uri, cupsLastErrorString());
      log_printf(cf_get_printer_attributes_log, "get-printer-attributes IPP request failed:\n");
      log_printf(cf_get_printer_attributes_log, "  - No response\n");
    }
    if (fallback == 1 + cap)
    {
      log_printf(cf_get_printer_attributes_log,
		 "No further fallback available, giving up\n");
      if (driverless_info != NULL)
        *driverless_info = CF_DRVLESS_CHECKERR;
    }
    else if (cap && fallback == 1)
    {
      log_printf(cf_get_printer_attributes_log,
		 "The server doesn't support the standard IPP request, trying request without media-col\n");
      if (driverless_info != NULL)
        *driverless_info = CF_DRVLESS_INCOMPLETEIPP;
    }
    else if (fallback == 0)
    {
      log_printf(cf_get_printer_attributes_log,
		 "The server doesn't support IPP2.0 request, trying IPP1.1 request\n");
      if (driverless_info != NULL)
        *driverless_info = CF_DRVLESS_IPP11;
    }
  }

  if (have_http == 0) httpClose(http_printer);
  if (uri) free(uri);
  return (NULL);
}

char*
cfippfindBasedURIConverter (const char *uri, int is_fax)
{
  int  ippfind_pid = 0,	        // Process ID of ippfind for IPP
       post_proc_pipe[2],	// Pipe to post-processing for IPP
       wait_children,		// Number of child processes left
       wait_pid,		// Process ID from wait()
       wait_status,		// Status from child
       exit_status = 0,		// Exit status
       bytes,
       port,
       i,
       output_of_fax_uri = 0,
       is_local;
  char *ippfind_argv[100],	// Arguments for ippfind
       *ptr_to_port = NULL,
       *reg_type,
       *resolved_uri = NULL,	// Buffer for resolved URI
       *resource_field = NULL,
       *service_hostname = NULL,
       // URI components...
       scheme[32],
       userpass[256],
       hostname[1024],
       resource[1024],
       *buffer = NULL,		// Copy buffer
       *ptr;			// Pointer into string;
  cups_file_t *fp;		// Post-processing input file
  int  status;			// Status of GET request

  status = httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme),
			   userpass, sizeof(userpass),
			   hostname, sizeof(hostname), &port, resource,
			   sizeof(resource));
  if (status < HTTP_URI_OK)
    // Invalid URI
    goto error;

  // URI is not DNS-SD-based, so do not resolve
  if ((reg_type = strstr(hostname, "._tcp")) == NULL)
    return (strdup(uri));

  resolved_uri =
    (char *)malloc(CF_GET_PRINTER_ATTRIBUTES_MAX_URI_LEN * (sizeof(char)));
  if (resolved_uri == NULL)
    goto error;
  memset(resolved_uri, 0, CF_GET_PRINTER_ATTRIBUTES_MAX_URI_LEN);

  reg_type --;
  while (reg_type >= hostname && *reg_type != '.')
    reg_type --;
  if (reg_type < hostname)
    goto error;
  *reg_type++ = '\0';

  i = 0;
  ippfind_argv[i++] = "ippfind";
  ippfind_argv[i++] = reg_type;           // list IPP(S) entries
  ippfind_argv[i++] = "-T";               // DNS-SD poll timeout
  ippfind_argv[i++] = "0";                // Minimum time required
  if (is_fax)
  {
    ippfind_argv[i++] = "--txt";
    ippfind_argv[i++] = "rfo";
  }
  ippfind_argv[i++] = "-N";
  ippfind_argv[i++] = hostname;
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             // Output the needed data fields
  ippfind_argv[i++] = "-en";              // separated by tab characters
  if(is_fax)
    ippfind_argv[i++] = "\n{service_hostname}\t{txt_rfo}\t{service_port}\t";
  else
    ippfind_argv[i++] = "\n{service_hostname}\t{txt_rp}\t{service_port}\t";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = "--local";          // Rest only if local service
  ippfind_argv[i++] = "-x";
  ippfind_argv[i++] = "echo";             // Output an 'L' at the end of the
  ippfind_argv[i++] = "-en";              // line
  ippfind_argv[i++] = "L";
  ippfind_argv[i++] = ";";
  ippfind_argv[i++] = NULL;

  //
  // Create a pipe for passing the ippfind output to post-processing
  //

  if (pipe(post_proc_pipe))
    goto error;

  if ((ippfind_pid = fork()) == 0)
  {
    //
    // Child comes here...
    //

    dup2(post_proc_pipe[1], 1);
    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);

    execvp(CUPS_IPPFIND, ippfind_argv);

    exit(1);
  }
  else if (ippfind_pid < 0)
  {
    //
    // Unable to fork!
    //

    goto error;
  }

  close(post_proc_pipe[1]);

  fp = cupsFileOpenFd(post_proc_pipe[0], "r");

  buffer =
    (char*)malloc(CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN * sizeof(char));
  if (buffer == NULL)
    goto error;
  memset(buffer, 0, CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN);

  while ((bytes =
	  cupsFileGetLine(fp, buffer,
			  CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN)) > 0)
  {
    // Mark all the fields of the output of ippfind
    ptr = buffer;

    // ignore new lines
    if (bytes < 3)
      goto read_error;

    // First, build the DNS-SD-service-name-based URI ...
    while (ptr && !isalnum(*ptr & 255)) ptr ++;

    service_hostname = ptr; 
    ptr = memchr(ptr, '\t',
		 CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    resource_field = ptr;
    ptr = memchr(ptr, '\t',
		 CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    ptr_to_port = ptr;
    ptr = memchr(ptr, '\t',
		 CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN - (ptr - buffer));
    if (!ptr) goto read_error;
    *ptr = '\0';
    ptr ++;

    // Do we have a local service so that we have to set the host name to
    // "localhost"?
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
      output_of_fax_uri = 1; // fax-uri requested from fax-capable device

  read_error:
    memset(buffer, 0, CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN);
  }

  cupsFileClose(fp);

  if (buffer != NULL)
    free(buffer);

  //
  // Wait for the child processes to exit...
  //

  wait_children = 1;

  while (wait_children > 0)
  {
    //
    // Wait until we get a valid process ID or the job is canceled...
    //

    while ((wait_pid = wait(&wait_status)) < 0 && errno == EINTR) {};

    if (wait_pid < 0)
      break;

    wait_children --;

    //
    // Report child status...
    //

    if (wait_status)
    {
      if (WIFEXITED(wait_status))
      {
	exit_status = WEXITSTATUS(wait_status);
        if (wait_pid == ippfind_pid && exit_status <= 2)
          exit_status = 0;	  
      }
      else if (WTERMSIG(wait_status) == SIGTERM)
      {
	// All OK, no error
      }
      else
      {
	exit_status = WTERMSIG(wait_status);
      }
    }
  }
  if (is_fax && !output_of_fax_uri)
    goto error;

  return (resolved_uri);

  //
  // Exit...
  //

 error:
  if (resolved_uri != NULL)
    free(resolved_uri);
  return (NULL);
}


const char* // O - Attribute value as string
cfIPPAttrEnumValForPrinter(ipp_t *printer_attrs, // I - Printer attributes, same
			                         //     as to respond
			                         //     get-printer-attributes,
			                         //     or NULL to not consider
			   ipp_t *job_attrs,     // I - Job attributes
			   const char *attr_name)// I - Attribute name
{
  ipp_attribute_t *attr;
  char printer_attr_name[256];
  int  i;
  const char *res;


  if ((printer_attrs == NULL && job_attrs == NULL) || attr_name == NULL)
    return NULL;

  // Check whether job got supplied the named attribute and read out its value
  // as string
  if (job_attrs == NULL ||
      (attr = ippFindAttribute(job_attrs, attr_name, IPP_TAG_ZERO)) == NULL)
    res = NULL;
  else
    res = ippGetString(attr, 0, NULL);

  // Check the printer properties if supplied to see whether the job attribute
  // value is valid or if the job attribute was not supplied. Use printer
  // default value of job attribute is invalid or not supplied
  // If no printer attributes are supplied (NULL), simply accept the job
  // attribute value
  if (printer_attrs)
  {
    if (res && res[0])
    {
      // Check whether value is valid according to printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-supported", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_ZERO)) != NULL)
      {
	for (i = 0; i < ippGetCount(attr); i ++)
	  if (strcasecmp(res, ippGetString(attr, i, NULL)) == 0)
	    break; // Job attribute value is valid
	if (i == ippGetCount(attr))
	  res = NULL; // Job attribute value is not valid
      }
    }
    if (!res || !res[0])
    {
      // Use default value from printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-default", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_ZERO)) != NULL)
	res = ippGetString(attr, 0, NULL);
    }
  }

  return (res);
}


int                 // O - 1: Success; 0: Error
cfIPPAttrIntValForPrinter(ipp_t *printer_attrs, // I - Printer attributes, same
						//     as to respond
			                        //     get-printer-attributes,
			                        //     or NULL to not consider
			  ipp_t *job_attrs,     // I - Job attributes
			  const char *attr_name,// I - Attribute name
			  int   *value)         // O - Attribute value as
                                                //     integer
{
  ipp_attribute_t *attr;
  char printer_attr_name[256];
  int  retval, val, min, max;

  if ((printer_attrs == NULL && job_attrs == NULL) || attr_name == NULL)
    return (0);

  // Check whether job got supplied the named attribute and read out its value
  // as integer
  if (job_attrs == NULL ||
      (attr = ippFindAttribute(job_attrs, attr_name, IPP_TAG_ZERO)) == NULL)
    retval = 0;
  else
  {
    retval = 1;
    val = ippGetInteger(attr, 0);
  }

  // Check the printer properties if supplied to see whether the job attribute
  // value is valid or if the job attribute was not supplied. Use printer
  // default value of job attribute is invalid or not supplied
  // If no printer attributes are supplied (NULL), simply accept the job
  // attribute value
  if (printer_attrs)
  {
    if (retval == 1)
    {
      // Check whether value is valid according to printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-supported", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_RANGE)) != NULL)
      {
	min = ippGetRange(attr, 0, &max);
	if (val < min || val > max)
	  retval = 0; // Job attribute value out of range
      }
    }
    if (retval == 0) {
      // Use default value from printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-default", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_ZERO)) != NULL) {
	retval = 1;
	val = ippGetInteger(attr, 0);
      }
    }
  }

  if (retval == 1)
    *value = val;
  return (retval);
}


int                 // O - 1: Success; 0: Error
cfIPPAttrResolutionForPrinter(ipp_t *printer_attrs,// I - Printer attributes
			      ipp_t *job_attrs,    // I - Job attributes
			      const char *attr_name,// I - Attribute name
			      int   *xres,         // O - X resolution (dpi)
			      int   *yres)         // O - Y resolution (dpi)
{
  int i;
  ipp_attribute_t *attr;
  char printer_attr_name[256];
  int  retval, x, y;
  ipp_res_t units;

  if ((printer_attrs == NULL && job_attrs == NULL))
    return (0);

  if (attr_name == NULL)
    attr_name = "printer-resolution";

  // Check whether job got supplied the named attribute and read out its value
  // as integer
  if (job_attrs == NULL ||
      (attr = ippFindAttribute(job_attrs, attr_name, IPP_TAG_ZERO)) == NULL)
    retval = 0;
  else
  {
    retval = 1;
    x = ippGetResolution(attr, 0, &y, &units);
    if (units == IPP_RES_PER_CM)
    {
      // Get resolutions in dpi
      x = (int)((float)x * 2.54);
      y = (int)((float)y * 2.54);
    }
  }

  // Check the printer properties if supplied to see whether the job attribute
  // value is valid or if the job attribute was not supplied. Use printer
  // default value of job attribute is invalid or not supplied
  // If no printer attributes are supplied (NULL), simply accept the job
  // attribute value
  if (printer_attrs)
  {
    if (retval == 1)
    {
      // Check whether value is valid according to printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-supported", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_RANGE)) != NULL)
      {
	for (i = 0; i < ippGetCount(attr); i ++)
	{
	  int sx, sy;
	  ipp_res_t su;
	  sx = ippGetResolution(attr, i, &sy, &su);
	  if (su == IPP_RES_PER_CM)
	  {
	    // Get resolutions in dpi
	    sx = (int)((float)sx * 2.54);
	    sy = (int)((float)sy * 2.54);
	  }
	  if ((x - sx) * (x - sx) < 10 &&
	      (y - sy) * (y - sy) < 10)
	    break; // Job attribute value is valid
	}
	if (i == ippGetCount(attr))
	  retval = 0; // Job attribute value is not valid
      }
    }
    if (retval == 0)
    {
      // Use default value from printer attributes
      snprintf(printer_attr_name, sizeof(printer_attr_name) - 1,
	       "%s-default", attr_name);
      if ((attr = ippFindAttribute(printer_attrs, printer_attr_name,
				   IPP_TAG_ZERO)) != NULL)
      {
	retval = 1;
	x = ippGetResolution(attr, 0, &y, &units);
	if (units == IPP_RES_PER_CM)
	{
	  // Get resolutions in dpi
	  x = (int)((float)x * 2.54);
	  y = (int)((float)y * 2.54);
	}
      }
    }
  }

  if (retval == 1)
  {
    *xres = x;
    *yres = y;
  }
  return retval;
}


int
cfIPPReverseOutput(ipp_t *printer_attrs,
		   ipp_t *job_attrs)
{
  int i;
  ipp_attribute_t *attr1, *attr2;
  const char *val1, *val2;
  char buf[1024];
  int length;

  // Figure out the right default output order from the IPP attributes...
  if ((val1 = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs,
					 "output-bin")) != NULL)
  {
    // Find corresponding "printer-output-tray" entry
    if ((attr1 = ippFindAttribute(printer_attrs, "output-bin-supported",
				  IPP_TAG_ZERO)) != NULL &&
	(attr2 = ippFindAttribute(printer_attrs, "printer-output-tray",
				  IPP_TAG_ZERO)) != NULL)
    {
      for (i = 0; i < ippGetCount(attr1) && i < ippGetCount(attr2); i ++)
      {
	if ((val2 = ippGetString(attr1, i, 0)) != NULL &&
	    strcmp(val1, val2) == 0)
	{
	  if ((val2 =
	       (const char *)ippGetOctetString(attr2, i, &length)) != NULL)
	  {
	    if (length > (int)(sizeof(buf) - 1))
	      length = (int)(sizeof(buf) - 1);
	    memcpy(buf, val2, length);
	    buf[length] = '\0';
	    if (strcasestr(buf, "stackingorder=firstToLast"))
	      return (0);
	    if (strcasestr(buf, "stackingorder=lastToFirst"))
	      return (1);
	    if (strcasestr(buf, "pagedelivery=faceDown"))
	      return (0);
	    if (strcasestr(buf, "pagedelivery=faceUp"))
	      return (1);
	  }
	  break;
	}
      }
    }
    // Check whether output bin name is "face-down" or "face-up"
    if (strcasestr(val1, "face-down"))
      return (0);
    if (strcasestr(val1, "face-up"))
      return (1);
  }

  // No hint of whether to print in original or reverse order. Usually this
  // happens for a fax-out queue, where one has no output bin. Use original
  // order then.
  return (0);
}


//
//  'cfGetBackSideOrientation()' - This functions returns the back
//				   side orientation using printer
//				   attributes.  Meaning and reason for
//				   backside orientation: It only makes
//				   sense if printer supports duplex,
//				   so, if printer reports that it
//				   supports duplex printing via
//				   sides-supported IPP attribute, then
//				   it also reports back-side
//				   orientation for each PDL in PDL
//				   specific IPP attributes. Backside
//				   orientation is specially needed for
//				   raster PDLs as raster PDLs are
//				   specially made for raster printers
//				   which do not have sufficient memory
//				   to hold a full page bitmap(raster
//				   page).  So they cannot build the
//				   whole page in memory before
//				   starting to print it. For one-sided
//				   printing it is easy to manage. The
//				   printer's mechanism pulls the page
//				   in on its upper edge and starts to
//				   print, from top to bottom, after
//				   that it ejects the page.  For
//				   double-sided printing it does the
//				   same for the front side, but for
//				   the back side the mechanics of the
//				   printer has to turn over the sheet,
//				   and now, depending on how the sheet
//				   is turned over it happens that the
//				   edge arriving in the printing
//				   mechanism is the lower edge of the
//				   back side. And if the printer
//				   simply prints then, the back side
//				   is the wrong way around. The
//				   printer reports its need via back
//				   side orientation in such a case, so
//				   that the client knows to send the
//				   back side upside down for example.
//				   In vector PDLs, PDF and PostScript,
//				   always the full page's raster image
//				   is completely generated in the
//				   printer before the page is started,
//				   and therefore the printer can start
//				   to take the pixels from the lower
//				   edge of the raster image if needed,
//				   so back side orientation is always
//				   "normal" for these PDLs.  And if a
//				   printer does not support duplex,
//				   back side orientation is not
//				   needed.
//

int				       // O - Backside orientation (bit 0-2)
				       //     Requires flipped margin?
				       //     Yes: bit 4 set; No: bit 3 set
cfGetBackSideOrientation(cf_filter_data_t *data) // I - Filter data
{
  ipp_t *printer_attrs = data->printer_attrs;
  int num_options = data->num_options;
  cups_option_t *options = data->options;
  char *final_content_type = data->final_content_type;
  ipp_attribute_t *ipp_attr = NULL; // IPP attribute
  int i,			// Looping variable
      count;
  const char *keyword;
  int backside = -1;	// backside obtained using printer attributes


  // also check options
  if ((ipp_attr = ippFindAttribute(printer_attrs, "sides-supported",
				   IPP_TAG_ZERO)) != NULL)
  {
    if (ippContainsString(ipp_attr, "two-sided-long-edge"))
    {
      if (final_content_type &&
	  strcasestr(final_content_type, "/urf") &&
	  (ipp_attr = ippFindAttribute(printer_attrs, "urf-supported",
				       IPP_TAG_ZERO)) != NULL)
      {
	for (i = 0, count = ippGetCount(ipp_attr); i < count; i ++)
	{
	  const char *dm = ippGetString(ipp_attr, i, NULL); // DM value
	  if (!strcasecmp(dm, "DM1"))
	  {
	    backside = CF_BACKSIDE_NORMAL;
	    break;
	  }
	  if (!strcasecmp(dm, "DM2"))
	  {
	    backside = CF_BACKSIDE_FLIPPED;
	    break;
	  }
	  if (!strcasecmp(dm, "DM3"))
	  {
	    backside = CF_BACKSIDE_ROTATED;
	    break;
	  }
	  if (!strcasecmp(dm, "DM4"))
	  {
	    backside = CF_BACKSIDE_MANUAL_TUMBLE;
	    break;
	  }
	}
      }
      else if ((final_content_type &&
		strcasestr(final_content_type, "/vnd.pwg-raster") &&
		(ipp_attr = ippFindAttribute(printer_attrs,
					     "pwg-raster-document-sheet-back",
					     IPP_TAG_ZERO)) != NULL) ||
	       (final_content_type &&
		strcasestr(final_content_type, "/pclm") &&
		(ipp_attr = ippFindAttribute(printer_attrs,
					     "pclm-raster-back-side",
					     IPP_TAG_ZERO)) != NULL) ||
	       ((ipp_attr = NULL) ||
		(keyword = cupsGetOption("back-side-orientation",
					 num_options, options)) != NULL))
      {
	if (ipp_attr)
	  keyword = ippGetString(ipp_attr, 0, NULL);
	if (!strcasecmp(keyword, "flipped"))
	  backside = CF_BACKSIDE_FLIPPED;
	else if (!strncasecmp(keyword, "manual", 6))
	  backside = CF_BACKSIDE_MANUAL_TUMBLE;
	else if (!strcasecmp(keyword, "normal"))
	  backside = CF_BACKSIDE_NORMAL;
	else if (!strcasecmp(keyword, "rotated"))
	  backside = CF_BACKSIDE_ROTATED;
      }

      if (backside == -1)
	backside = CF_BACKSIDE_NORMAL;
      else if ((keyword = cupsGetOption("duplex-requires-flipped-margin",
					num_options, options)) != NULL)
      {
	if (strcasecmp(keyword, "true") == 0)
	  backside |= 16;
	else
	  backside |= 8;
      }
    }
  }

  return (backside);
}


const char *
cfGetPrintRenderIntent(cf_filter_data_t *data,
		       char *ri,
		       int ri_len)
{
  const char		*val;
  int 			num_options = 0;
  cups_option_t 	*options = NULL;
  ipp_t 		*printer_attrs = data->printer_attrs;
  ipp_attribute_t 	*ipp_attr;
  cf_logfunc_t 	        log = data->logfunc;
  void                  *ld = data->logdata;
  int 			i;


  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  if ((val = cupsGetOption("print-rendering-intent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PrintRenderingIntent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("RenderingIntent", num_options,
			   options)) != NULL)
  {
    if (!strcasecmp(val, "absolute"))
      snprintf(ri, ri_len, "%s", "Absolute");
    else if (!strcasecmp(val, "auto") || !strcasecmp(val, "automatic"))
      snprintf(ri, ri_len, "%s", "Automatic");
    else if (!strcasecmp(val, "perceptual"))
      snprintf(ri, ri_len, "%s", "Perceptual");
    else if (!strcasecmp(val, "relative"))
      snprintf(ri, ri_len, "%s", "Relative");
    else if (!strcasecmp(val, "relative-bpc") ||
	     !strcasecmp(val, "relativebpc"))
      snprintf(ri, ri_len, "%s", "RelativeBpc");
    else if (!strcasecmp(val, "saturation"))
      snprintf(ri, ri_len, "%s", "Saturation");
  }

  if ((ipp_attr = ippFindAttribute(printer_attrs,
				   "print-rendering-intent-supported",
				   IPP_TAG_ZERO)) != NULL)
  {
    int autoRender = 0;
    int count;

    if ((count = ippGetCount(ipp_attr)) > 0)
    {
      for (i = 0; i < count; i ++)
      {
	const char *temp = ippGetString(ipp_attr, i, NULL);
	if (!strcasecmp(temp, "auto")) autoRender = 1;
	if (ri[0] != '\0')
	  // User has supplied a setting
	  if (!strcasecmp(ri, temp))
	    break;
      }
      if (ri[0] != '\0' && i == count)
      {
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "User specified print-rendering-intent not supported "
		     "by printer, using default print rendering intent.");
	ri[0] = '\0';
      }
      if (ri[0] == '\0')
      {	// Either user has not supplied any setting
	// or user supplied value is not supported by printer
	if ((ipp_attr = ippFindAttribute(printer_attrs,
					 "print-rendering-intent-default",
					 IPP_TAG_ZERO)) != NULL)
	  snprintf(ri, ri_len, "%s", ippGetString(ipp_attr, 0, NULL));
	else if (autoRender == 1)
	  snprintf(ri, ri_len, "%s", "auto");
      }
    }
  }

  cupsFreeOptions(num_options, options);
  return (ri);
}


//
// 'cfJoinJobOptionsAndAttrs()' - Function for storing job IPP attribute in
//                                option list, together with the options
//

int                                               // O  - New number of options
                                                  //      in new option list
cfJoinJobOptionsAndAttrs(cf_filter_data_t* data,  // I  - Filter data
			 int num_options,         // I  - Current mumber of
			                          //      options in new option
			                          //      list
			 cups_option_t **options) // IO - New option lsit
{
  ipp_t *job_attrs = data->job_attrs;   // Job attributes
  ipp_attribute_t *ipp_attr;            // IPP attribute
  int i = 0;                            // Looping variable
  char buf[2048];                       // Buffer for storing value of ipp attr
  cups_option_t *opt;

  for (i = 0, opt = data->options; i < data->num_options; i ++, opt ++)
    num_options = cupsAddOption(opt->name, opt->value, num_options, options);

  for (ipp_attr = ippFirstAttribute(job_attrs); ipp_attr;
       ipp_attr = ippNextAttribute(job_attrs))
  {
    ippAttributeString(ipp_attr, buf, sizeof(buf));
    num_options = cupsAddOption(ippGetName(ipp_attr), buf,
				num_options, options);
  }

  return (num_options);
}


#ifndef HAVE_STRLCPY
//
// 'strlcpy()' - Safely copy two strings.
//

size_t					// O - Length of string
strlcpy(char       *dst,		// O - Destination string
	const char *src,		// I - Source string
	size_t      size)		// I - Size of destination string buffer
{
  size_t	srclen;			// Length of source string


  //
  // Figure out how much room is needed...
  //

  size --;

  srclen = strlen(src);

  //
  // Copy the appropriate amount...
  //

  if (srclen > size)
    srclen = size;

  memmove(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}
#endif // !HAVE_STRLCPY

//
// 'cfStrFormatd()' - Format a floating-point number.
//

char *					// O - Pointer to end of string
cfStrFormatd(char         *buf,		// I - String
	     char         *bufend,	// I - End of string buffer
	     double       number,	// I - Number to format
	     struct lconv *loc)		// I - Locale data
{
  char		*bufptr,		// Pointer into buffer
		temp[1024],		// Temporary string
		*tempdec,		// Pointer to decimal point
		*tempptr;		// Pointer into temporary string
  const char	*dec;			// Decimal point
  int		declen;			// Length of decimal point


  //
  // Format the number using the "%.12f" format and then eliminate
  // unnecessary trailing 0's.
  //

  snprintf(temp, sizeof(temp), "%.12f", number);
  for (tempptr = temp + strlen(temp) - 1;
       tempptr > temp && *tempptr == '0';
       *tempptr-- = '\0');

  //
  // Next, find the decimal point...
  //

  if (loc && loc->decimal_point) {
    dec    = loc->decimal_point;
    declen = (int)strlen(dec);
  } else {
    dec    = ".";
    declen = 1;
  }

  if (declen == 1)
    tempdec = strchr(temp, *dec);
  else
    tempdec = strstr(temp, dec);

  //
  // Copy everything up to the decimal point...
  //

  if (tempdec) {
    for (tempptr = temp, bufptr = buf;
         tempptr < tempdec && bufptr < bufend;
	 *bufptr++ = *tempptr++);

    tempptr += declen;

    if (*tempptr && bufptr < bufend) {
      *bufptr++ = '.';

      while (*tempptr && bufptr < bufend)
        *bufptr++ = *tempptr++;
    }

    *bufptr = '\0';
  } else {
    strlcpy(buf, temp, (size_t)(bufend - buf + 1));
    bufptr = buf + strlen(buf);
  }

  return (bufptr);
}


int
cfCompareResolutions(void *resolution_a,
		     void *resolution_b,
		     void *user_data)
{
  cf_res_t *res_a = (cf_res_t *)resolution_a;
  cf_res_t *res_b = (cf_res_t *)resolution_b;
  int i, a, b;

  // Compare the pixels per square inch
  a = res_a->x * res_a->y;
  b = res_b->x * res_b->y;
  i = (a > b) - (a < b);
  if (i) return (i);

  // Compare how much the pixel shape deviates from a square, the
  // more, the worse
  a = 100 * res_a->y / res_a->x;
  if (a > 100) a = 10000 / a; 
  b = 100 * res_b->y / res_b->x;
  if (b > 100) b = 10000 / b; 
  return ((a > b) - (a < b));
}

void *
cfCopyResolution(void *resolution,
		 void *user_data)
{
  cf_res_t *res = (cf_res_t *)resolution;
  cf_res_t *copy;

  copy = (cf_res_t *)calloc(1, sizeof(cf_res_t));
  if (copy)
  {
    copy->x = res->x;
    copy->y = res->y;
  }

  return copy;
}

void
cfFreeResolution(void *resolution,
		 void *user_data)
{
  cf_res_t *res = (cf_res_t *)resolution;

  if (res) free(res);
}

cups_array_t *
cfNewResolutionArray()
{
  return (cupsArrayNew3(cfCompareResolutions, NULL, NULL, 0,
			cfCopyResolution, cfFreeResolution));
}

cf_res_t *
cfNewResolution(int x,
		int y)
{
  cf_res_t *res = (cf_res_t *)calloc(1, sizeof(cf_res_t));
  if (res)
  {
    res->x = x;
    res->y = y;
  }
  return (res);
}

// Read a single resolution from an IPP attribute, take care of
// obviously wrong entries (printer firmware bugs), ignoring
// resolutions of less than 60 dpi in at least one dimension and
// fixing Brother's "600x2dpi" resolutions.
cf_res_t *
cfIPPResToResolution(ipp_attribute_t *attr,
		     int index)
{
  cf_res_t *res = NULL;
  int x = 0, y = 0;
  ipp_res_t units;

  if (attr)
  {
    ipp_tag_t tag = ippGetValueTag(attr);
    int count = ippGetCount(attr);

    if (tag == IPP_TAG_RESOLUTION && index < count)
    {
      x = ippGetResolution(attr, index, &y, &units);
      if (units == IPP_RES_PER_CM)
      {
	x = (int)(x * 2.54);
	y = (int)(y * 2.54);
      }
      if (y == 2) y = x; // Brother quirk ("600x2dpi")
      if (x >= 60 && y >= 60)
	res = cfNewResolution(x, y);
    }
  }

  return (res);
}

cups_array_t *
cfIPPAttrToResolutionArray(ipp_attribute_t *attr)
{
  cups_array_t *res_array = NULL;
  cf_res_t *res;
  int i;

  if (attr)
  {
    ipp_tag_t tag = ippGetValueTag(attr);
    int count = ippGetCount(attr);

    if (tag == IPP_TAG_RESOLUTION && count > 0)
    {
      res_array = cfNewResolutionArray();
      if (res_array)
      {
	for (i = 0; i < count; i ++)
	  if ((res = cfIPPResToResolution(attr, i)) != NULL)
	  {
	    if (cupsArrayFind(res_array, res) == NULL)
	      cupsArrayAdd(res_array, res);
	    cfFreeResolution(res, NULL);
	  }
      }
      if (cupsArrayCount(res_array) == 0)
      {
	cupsArrayDelete(res_array);
	res_array = NULL;
      }
    }
  }

  return (res_array);
}

// Build up an array of common resolutions and most desirable default
// resolution from multiple arrays of resolutions with an optional
// default resolution.
// Call this function with each resolution array you find as "new", and
// in "current" an array of the common resolutions will be built up.
// You do not need to create an empty array for "current" before
// starting. Initialize it with NULL.
// "current_default" holds the default resolution of the array "current".
// It will get replaced by "new_default" if "current_default" is either
// NULL or a resolution which is not in "current" any more.
// "new" and "new_default" will be deleted/freed and set to NULL after
// each, successful or unsuccssful operation.
// Note that when calling this function the addresses of the pointers
// to the resolution arrays and default resolutions have to be given
// (call by reference) as all will get modified by the function.

int // 1 on success, 0 on failure
cfJoinResolutionArrays(cups_array_t **current,
		       cups_array_t **new_arr,
		       cf_res_t **current_default,
		       cf_res_t **new_default)
{
  cf_res_t *res;
  int retval;

  if (current == NULL || new_arr == NULL || *new_arr == NULL ||
      cupsArrayCount(*new_arr) == 0)
  {
    retval = 0;
    goto finish;
  }

  if (*current == NULL)
  {
    // We are adding the very first resolution array, simply make it
    // our common resolutions array
    *current = *new_arr;
    if (current_default)
    {
      if (*current_default)
	free(*current_default);
      *current_default = (new_default ? *new_default : NULL);
    }
    return 1;
  }
  else if (cupsArrayCount(*current) == 0)
  {
    retval = 1;
    goto finish;
  }

  // Dry run: Check whether the two arrays have at least one resolution
  // in common, if not, do not touch the original array
  for (res = cupsArrayFirst(*current);
       res; res = cupsArrayNext(*current))
    if (cupsArrayFind(*new_arr, res))
      break;

  if (res)
  {
    // Reduce the original array to the resolutions which are in both
    // the original and the new array, at least one resolution will
    // remain.
    for (res = cupsArrayFirst(*current);
	 res; res = cupsArrayNext(*current))
      if (!cupsArrayFind(*new_arr, res))
	cupsArrayRemove(*current, res);
    if (current_default)
    {
      // Replace the current default by the new one if the current default
      // is not in the array any more or if it is NULL. If the new default
      // is not in the list or NULL in such a case, set the current default
      // to NULL
      if (*current_default && !cupsArrayFind(*current, *current_default))
      {
	free(*current_default);
	*current_default = NULL;
      }
      if (*current_default == NULL && new_default && *new_default &&
	  cupsArrayFind(*current, *new_default))
	*current_default = cfCopyResolution(*new_default, NULL);
    }
    retval = 1;
  } else
    retval = 0;

 finish:
  if (new_arr && *new_arr)
  {
    cupsArrayDelete(*new_arr);
    *new_arr = NULL;
  }
  if (new_default && *new_default)
  {
    free(*new_default);
    *new_default = NULL;
  }

  return (retval);
}


//
// 'pwg_compare_sizes()' - Compare two media sizes...
//

static int				// O - Result of comparison
pwg_compare_sizes(cups_size_t *a,	// I - First media size
                  cups_size_t *b)	// I - Second media size
{
  return (strcmp(a->media, b->media));
}


//
// 'pwg_copy_size()' - Copy a media size.
//

static cups_size_t *			// O - New media size
pwg_copy_size(cups_size_t *size)	// I - Media size to copy
{
  cups_size_t	*newsize = (cups_size_t *)calloc(1, sizeof(cups_size_t));
					// New media size

  if (newsize)
    memcpy(newsize, size, sizeof(cups_size_t));

  return (newsize);
}


int					// O -  1: Requested page size supported
                                        //      2: Requested page size supported
                                        //	   when rotated by 90 degrees
                                        //      0: No page size requested
                                        //     -1: Requested size unsupported
cfGetPageDimensions(ipp_t *printer_attrs,   // I - Printer attributes
	            ipp_t *job_attrs,	    // I - Job attributes
	            int num_options,        // I - Number of options
	            cups_option_t *options, // I - Options
		    cups_page_header2_t *header, // I - Raster page header
		    int transverse_fit,     // I - Accept transverse fit?
	            float *width,	    // O - Width (in pt, 1/72 inches)
		    float *height,          // O - Height
		    float *left,            // O - Left margin
		    float *bottom,          // O - Bottom margin
		    float *right,           // O - Right margin
		    float *top,             // O - Top margin
		    char *name,             // O - Page size name
		    ipp_t **media_col_entry)// O - media-col-database record of
                                            //     match
{
  int           i;
  const char    *attr_name;
  char          size_name_buf[IPP_MAX_NAME + 1];
  int           size_requested = 0;
  const char * const media_size_attr_names[] =
  {
    "Jmedia-col",
    "Jmedia-size",
    "Jmedia",
    "JPageSize",
    "JMediaSize",
    "J", // A raster header with media dimensions
    "jmedia-col",
    "jmedia-size",
    "jmedia",
    "jPageSize",
    "jMediaSize",
    "j", // A raster header with media dimensions
    "Dmedia-col-default",
    "Dmedia-default",
  };


  if (name == NULL)
    name = size_name_buf;
  name[0] = '\0';

  if (media_col_entry)
    *media_col_entry = NULL;

  //
  // Media from job_attrs and options, defaults from printer_attrs...
  //

  // Go through all job attributes and options which could contain the
  // page size, afterwards go through the page size defaults in the
  // printer attributes
  for (i = 0;
       i < sizeof(media_size_attr_names) / sizeof(media_size_attr_names[0]);
       i ++)
  {
    ipp_attribute_t *attr = NULL;	// Job attribute
    char	valstr[8192];		// Attribute value string
    const char	*value = NULL;		// Option value
    const char	*name_ptr = NULL;	// Pointer to page size name
    int		num_media_col = 0;	// Number of media-col values
    cups_option_t *media_col = NULL;	// media-col values
    int         ipp_width = 0,
                ipp_height = 0,
                ipp_left = -1,
                ipp_bottom = -1,
                ipp_right = -1,
                ipp_top = -1;

    attr_name = media_size_attr_names[i];
    if (*attr_name == 'J' ||
	(transverse_fit && *attr_name == 'j')) // Job attribute/option
    {
      if (*(attr_name + 1) == '\0')
      {
	if (header)
	{
	  // Raster header
	  if (header->cupsPageSize[0] > 0.0)
	    ipp_width = (int)(header->cupsPageSize[0] * 2540.0 / 72.0);
	  else if (header->PageSize[0] > 0)
	    ipp_width = (int)(header->PageSize[0] * 2540 / 72);
	  if (header->cupsPageSize[1] > 0.0)
	    ipp_height = (int)(header->cupsPageSize[1] * 2540.0 / 72.0);
	  else if (header->PageSize[1] > 0)
	    ipp_height = (int)(header->PageSize[1] * 2540 / 72);
	  if (header->ImagingBoundingBox[3] > 0)
	  {
	    if (header->cupsImagingBBox[0] >= 0.0)
	      ipp_left = (int)(header->cupsImagingBBox[0] * 2540.0 / 72.0);
	    else if (header->ImagingBoundingBox[0] >= 0)
	      ipp_left = (int)(header->ImagingBoundingBox[0] * 2540 / 72);
	    if (header->cupsImagingBBox[1] >= 0.0)
	      ipp_bottom = (int)(header->cupsImagingBBox[1] * 2540.0 / 72.0);
	    else if (header->ImagingBoundingBox[1] >= 0)
	      ipp_bottom = (int)(header->ImagingBoundingBox[1] * 2540 / 72);
	    if (header->cupsImagingBBox[2] > 0.0)
	      ipp_right = ipp_width -
	      (int)(header->cupsImagingBBox[2] * 2540.0 / 72.0);
	    else if (header->ImagingBoundingBox[2] > 0)
	      ipp_right = ipp_width -
		(int)(header->ImagingBoundingBox[2] * 2540 / 72);
	    if (header->cupsImagingBBox[3] > 0.0)
	    ipp_top = ipp_height -
	      (int)(header->cupsImagingBBox[3] * 2540.0 / 72.0);
	    else if (header->ImagingBoundingBox[3] > 0)
	      ipp_top = ipp_height -
		(int)(header->ImagingBoundingBox[3] * 2540 / 72);
	  }
	  else
	    ipp_left = ipp_bottom = ipp_right = ipp_top = 0;
	}
	else
	  continue;
      }
      else if ((attr = ippFindAttribute(job_attrs, attr_name + 1,
					IPP_TAG_ZERO)) != NULL)
      {
	// String from IPP attribute
	ippAttributeString(attr, valstr, sizeof(valstr));
	value = valstr;
      }
      else if ((value = cupsGetOption(attr_name + 1, num_options,
				      options)) == NULL)
	continue;
    }
    else if (*attr_name == 'D') // Printer default
    {
      if (*(attr_name + 1))
      {
	if ((attr = ippFindAttribute(printer_attrs, attr_name + 1,
				   IPP_TAG_ZERO)) != NULL)
        {
	  // String from IPP attribute
	  ippAttributeString(attr, valstr, sizeof(valstr));
	  value = valstr;
	}
	else
	  continue;
      }
      else
	continue;
    }
    else
      continue;

    if (value)
    {
      if (*value == '{')
      {
	//
	// String is a dictionary -> "media-col" value...
	//

	num_media_col = cupsParseOptions(value, 0, &media_col);

	// Actual size in dictionary?
	if ((value = cupsGetOption("media-size", num_media_col, media_col))
	    != NULL)
	{
	  int		num_media_size;	// Number of media-size values
	  cups_option_t *media_size;	// media-size values
	  const char	*x_dimension,	// x-dimension value
	                *y_dimension;	// y-dimension value

	  num_media_size = cupsParseOptions(value, 0, &media_size);

	  if ((x_dimension = cupsGetOption("x-dimension", num_media_size, media_size)) != NULL && (y_dimension = cupsGetOption("y-dimension", num_media_size, media_size)) != NULL)
	  {
	    ipp_width = atoi(x_dimension);
	    ipp_height = atoi(y_dimension);
	  }

	  cupsFreeOptions(num_media_size, media_size);
	}
	// Name in dictionary? Use only if actual dimensions are not supplied
	if ((ipp_width <= 0 || ipp_height <= 0) &&
	    (name_ptr = cupsGetOption("media-size-name",
				    num_media_col, media_col)) == NULL)
	{
	  cupsFreeOptions(num_media_col, media_col);
	  continue;
	}

	// Grab margins from media-col
	if ((value = cupsGetOption("media-left-margin",
				   num_media_col, media_col))
	    != NULL)
	  ipp_left = atoi(value);
	if ((value = cupsGetOption("media-bottom-margin",
				   num_media_col, media_col))
	    != NULL)
	  ipp_bottom = atoi(value);
	if ((value = cupsGetOption("media-right-margin",
				   num_media_col, media_col))
	    != NULL)
	  ipp_right = atoi(value);
	if ((value = cupsGetOption("media-top-margin",
				   num_media_col, media_col))
	    != NULL)
	  ipp_top = atoi(value);
      }
      else
      {
	//
	// String is not dictionary, check also if it contains commas (list
	// of media properties supplied via "media" CUPS option
	//

	char *ptr;
	name_ptr = value;
	if (strchr(value, ','))
	{
	  // Comma-separated list of media properties, supplied with "media"
	  // CUPS option
	  if (value != valstr)
	  {
	    // Copy string for further manipulation
	    strlcpy(valstr, value, sizeof(valstr));
	    value = valstr;
	    name_ptr = value;
	  }
	  for (ptr = (char *)value; *ptr;)
	  {
	    ptr ++;
	    if (*ptr == ',' || *ptr == '\0')
	    {
	      // End of item name
	      if (*ptr == ',')
	      {
		*ptr = '\0';
		ptr ++;
	      }
	      // Find PWG media entry for the name, if we find one, the name
	      // is actually a page size name
	      if (pwgMediaForPWG(name_ptr) ||
		  pwgMediaForPPD(name_ptr) ||
		  pwgMediaForLegacy(name_ptr))
		// This is a page size name
		break;
	      else if (*ptr)
		// Next item
		name_ptr = ptr;
	      else
		// No further item
		name_ptr = NULL;
	    }
	  }
	}
      }
    }

    // Get name from media
    if (name_ptr)
    {
      if (ipp_left == 0 && ipp_bottom == 0 &&
	  ipp_right == 0 && ipp_top == 0)
	snprintf(name, IPP_MAX_NAME, "%.29s.Borderless", name_ptr);
      else
	strlcpy(name, name_ptr, IPP_MAX_NAME);
    }

    // Landscape/Transverse fit
    if (*attr_name == 'j') // Only job attributes/options
    {
      int swap;

      swap = ipp_width;
      ipp_width = ipp_height;
      ipp_height = swap;

      swap = ipp_left;
      ipp_left = ipp_top;
      ipp_top = ipp_right;
      ipp_right = ipp_bottom;
      ipp_bottom = swap;	
    }
    
    cupsFreeOptions(num_media_col, media_col);

    // We have a valid request for a page size
    if (*attr_name == 'J' || *attr_name == 'j')
      size_requested = 1;

    // Validate collected information
    // If we have a size, we use the size as search term (name = "" then),
    // if we have no size but a name, use the name, always pass in margins
    // if available
    cfGenerateSizes(printer_attrs, CF_GEN_SIZES_SEARCH, NULL, NULL,
		    &ipp_width, &ipp_height,
		    &ipp_left, &ipp_bottom, &ipp_right, &ipp_top,
		    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, name,
		    media_col_entry);

    // Return resulting numbers
    if (ipp_width > 0 && ipp_height > 0 &&
	ipp_left >= 0 && ipp_bottom >= 0 &&
	ipp_right >= 0 && ipp_top >= 0)
    {
      if (width)
	*width = ipp_width * 72.0 / 2540.0;
      if (height)
	*height = ipp_height * 72.0 / 2540.0;

      if (left)
	*left = ipp_left * 72.0 / 2540.0;
      if (bottom)
	*bottom = ipp_bottom * 72.0 / 2540.0;
      if (right)
	*right = ipp_right * 72.0 / 2540.0;
      if (top)
	*top = ipp_top * 72.0 / 2540.0;

      return (*attr_name == 'J' ? 1 :
	      (*attr_name == 'j' ? 2 :
	       (size_requested ? -1 : 0)));
    }
    else if (media_col_entry)
      *media_col_entry = NULL;
  }
  return (size_requested ? -1 : 0);
}


void
cfGenerateSizes(ipp_t *response,
		cf_gen_sizes_mode_t mode,
		cups_array_t **sizes,
		ipp_attribute_t **defattr,
		int *width,
		int *length,
		int *left,
		int *bottom,
		int *right,
		int *top,
		int *min_width,
		int *min_length,
		int *max_width,
		int *max_length,
		int *custom_left,
		int *custom_bottom,
		int *custom_right,
		int *custom_top,
		char *size_name,
		ipp_t **media_col_entry)
{
  ipp_attribute_t          *default_attr,
                           *attr,                // xxx-supported
                           *x_dim, *y_dim,       // Media dimensions
                           *name;                // Media size name
  ipp_t                    *media_col,           // Media collection
                           *media_size;          // Media size collection
  int                      i, x = 0, y = 0, count = 0;
  pwg_media_t              *pwg, *pwg_by_name;   // PWG media size
  int                      local_min_width, local_min_length,
                           local_max_width, local_max_length;
  int                      local_left, local_right, local_bottom, local_top;
  ipp_attribute_t          *margin;  // media-xxx-margin attribute
  const char               *psname;
  const char               *entry_name;
  char                     size_name_buf[IPP_MAX_NAME + 1] = "";
  pwg_media_t              *search = NULL;
  int                      search_width = 0,
                           search_length = 0,
                           search_left = -1,
                           search_bottom = -1,
                           search_right = -1,
                           search_top = -1,
                           borderless = 0;
  long long                min_border_mismatch = LLONG_MAX,
                           border_mismatch;


  if (media_col_entry)
    *media_col_entry = NULL;

  if (custom_left == NULL)
    custom_left = &local_left;
  if ((attr = ippFindAttribute(response, "media-left-margin-supported",
			       IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, *custom_left = ippGetInteger(attr, 0),
	   count = ippGetCount(attr);
	 i < count; i ++)
      if (ippGetInteger(attr, i) < *custom_left)
        *custom_left = ippGetInteger(attr, i);
  } else
    *custom_left = 635;

  if (custom_bottom == NULL)
    custom_bottom = &local_bottom;
  if ((attr = ippFindAttribute(response, "media-bottom-margin-supported",
			       IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, *custom_bottom = ippGetInteger(attr, 0),
	   count = ippGetCount(attr);
	 i < count; i ++)
      if (ippGetInteger(attr, i) < *custom_bottom)
        *custom_bottom = ippGetInteger(attr, i);
  } else
    *custom_bottom = 1270;

  if (custom_right == NULL)
    custom_right = &local_right;
  if ((attr = ippFindAttribute(response, "media-right-margin-supported",
			       IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, *custom_right = ippGetInteger(attr, 0),
	   count = ippGetCount(attr);
	 i < count; i ++)
      if (ippGetInteger(attr, i) < *custom_right)
        *custom_right = ippGetInteger(attr, i);
  } else
    *custom_right = 635;

  if (custom_top == NULL)
    custom_top = &local_top;
  if ((attr = ippFindAttribute(response, "media-top-margin-supported",
			       IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, *custom_top = ippGetInteger(attr, 0),
	   count = ippGetCount(attr);
	 i < count; i ++)
      if (ippGetInteger(attr, i) < *custom_top)
        *custom_top = ippGetInteger(attr, i);
  } else
    *custom_top = 1270;

  if (mode != CF_GEN_SIZES_DEFAULT)
  {
    if (min_width == NULL)
      min_width = &local_min_width;
    *min_width = 0;
    if (min_length == NULL)
      min_length = &local_min_length;
    *min_length = 0;
    if (max_width == NULL)
      max_width = &local_max_width;
    *max_width = 0;
    if (max_length == NULL)
      max_length = &local_max_length;
    *max_length = 0;
  }

  if (size_name == NULL)
  {
    size_name = size_name_buf;
    size_name[0] = '\0';
  }
  if (mode == CF_GEN_SIZES_DEFAULT)
    size_name[0] = '\0';
  if (defattr == NULL && mode == CF_GEN_SIZES_DEFAULT)
    defattr = &default_attr;
  if (defattr &&
      (*defattr = ippFindAttribute(response, "media-col-default",
				   IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    if (mode == CF_GEN_SIZES_DEFAULT &&
	(attr = ippFindAttribute(ippGetCollection(*defattr, 0), "media-size",
				 IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      media_size = ippGetCollection(attr, 0);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);
  
      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-bottom-margin", IPP_TAG_INTEGER))
	  != NULL)
	local_bottom = ippGetInteger(margin, 0);
      else
	local_bottom = *custom_bottom;
      if (bottom)
	*bottom = local_bottom;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-left-margin", IPP_TAG_INTEGER))
	  != NULL)
	local_left = ippGetInteger(margin, 0);
      else
	local_left = *custom_left;
      if (left)
	*left = local_left;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-right-margin", IPP_TAG_INTEGER))
	  != NULL)
	local_right = ippGetInteger(margin, 0);
      else
	local_right = *custom_right;
      if (right)
	*right = local_right;

      if ((margin = ippFindAttribute(ippGetCollection(*defattr, 0),
				     "media-top-margin", IPP_TAG_INTEGER))
	  != NULL)
	local_top = ippGetInteger(margin, 0);
      else
	local_top = *custom_top;
      if (top)
	*top = local_top;

      if (x_dim && y_dim)
      {
	x = ippGetInteger(x_dim, 0);
	y = ippGetInteger(y_dim, 0);
	if (x > 0 && y > 0 &&
	    (pwg = pwgMediaForSize(x, y)) != NULL) {
	  psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	  if (local_bottom == 0 && local_left == 0 &&
	      local_right == 0 && local_top == 0)
	    snprintf(size_name, IPP_MAX_NAME, "%s.Borderless", psname);
	  else
	    strlcpy(size_name, psname, IPP_MAX_NAME);
	}
      }
    }
  }
  if (mode == CF_GEN_SIZES_DEFAULT &&
      (pwg =
       pwgMediaForPWG(ippGetString(ippFindAttribute(response,
						    "media-default",
						    IPP_TAG_ZERO), 0,
				   NULL))) != NULL)
  {
    psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
    strlcpy(size_name, psname, IPP_MAX_NAME);
    if (x <= 0 || y <= 0)
    {
      x = pwg->width;
      y = pwg->length;
    }
  }

  if (mode == CF_GEN_SIZES_DEFAULT)
  {
    // Output the default page size dimensions, 0 if no valid size found
    if (!size_name[0])
      strlcpy(size_name, "Unknown", IPP_MAX_NAME);
    if (width)
    {
      if (x > 0)
	*width = x;
      else
	*width = 0;
    }
    if (length)
    {
      if (y > 0)
	*length = y;
      else
	*length = 0;
    }
  }
  else
  {
    // Find the dimensions for the page size name we got as search term
    char *ptr;
    int is_transverse = (strcasestr(size_name, ".Transverse") ? 1 : 0);
    if (strcasestr(size_name, ".Fullbleed") ||
	strcasestr(size_name, ".Borderless") ||
	strcasestr(size_name, ".FB"))
      mode = CF_GEN_SIZES_SEARCH_BORDERLESS_ONLY;
    if (size_name != size_name_buf)
      strlcpy(size_name_buf, size_name, IPP_MAX_NAME);
    if ((ptr = strchr(size_name_buf, '.')) != NULL &&
	strncasecmp(size_name_buf, "Custom.", 7) != 0)
      *ptr = '\0';
    if ((search = pwgMediaForPWG(size_name_buf)) == NULL)
      if ((search = pwgMediaForPPD(size_name_buf)) == NULL)
	search = pwgMediaForLegacy(size_name_buf);
    if (search != NULL)
    {
      // Set the appropriate dimensions
      if (is_transverse)
      {
	search_width = search->length;
	search_length = search->width;
      }
      else
      {
	search_width = search->width;
	search_length = search->length;
      }
    }
    else
    {
      // Set the dimensions if we search by dimensions
      if (width)
	search_width = *width;
      if (length)
	search_length = *length;
    }
    if (search_width <= 0 || search_length <= 0)
    {
      // No valid search dimensions, de-activate searching and set 0 as
      // result
      mode = CF_GEN_SIZES_DEFAULT;
      if (width)
	*width = 0;
      if (length)
	*length = 0;
    }
    else
    {
      // Check whether we have margin info so that we can search for a
      // size with similar/the same margins, otherwise set the margins
      // -1 to pick the first entry from the list which fits the size
      // dimensions (if there are variants of a size, the first entry
      // is usually the standard size)
      if (left && *left >= 0)
	search_left = *left;
      else
	search_left = -1;
      if (bottom && *bottom >= 0)
	search_bottom = *bottom;
      else
	search_bottom = -1;
      if (right && *right >= 0)
	search_right = *right;
      else
	search_right = -1;
      if (top && *top >= 0)
	search_top = *top;
      else
	search_top = -1;
    }
  }

  if (mode == CF_GEN_SIZES_DEFAULT &&
      !sizes && !min_width && !max_width && !min_length && !max_length)
    return;

  if (sizes)
    *sizes = cupsArrayNew3((cups_array_func_t)pwg_compare_sizes, NULL, NULL, 0,
			   (cups_acopy_func_t)pwg_copy_size,
			   (cups_afree_func_t)free);

  if ((attr = ippFindAttribute(response, "media-col-database",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      cups_size_t temp, temp_by_name;   // Current size

      media_col   = ippGetCollection(attr, i);
      media_size  =
	ippGetCollection(ippFindAttribute(media_col, "media-size",
					  IPP_TAG_BEGIN_COLLECTION), 0);

      // These are the numeric paper dimensions explicitly mentioned
      // in this media entry. if we were called in a retro-fitting
      // setup via a ppdFilter...() wrapper filter function of libppd,
      // these dimensions can deviate from the paper dimensions which
      // the PWG-style page size name in the same entry suggests. In
      // this case we match both sizes against the size requested for
      // the job and consider the entry as matching if one of the two
      // sizes matches. In this case the entry gets included in all
      // entries which are selected by the closest fit of the margins.
      //
      // We do this as some PPD files (especially of HPLIP) contain
      // page size entries which are variants of a standard size with
      // the base name of a standard size (like "A4.Borderless", base
      // name "A4") but different dimensions.
      //
      // Especially there are larger dimensions for borderless, for
      // overspraying over the borders of the sheet so that there will
      // be no faint white borders if the sheet is a little
      // mis-aligned.
      //
      // So if such overspraying borderless size entry is present and
      // has zero margins while the standard size entry has regular
      // margins, this entry will get automatically selected if
      // borderless printing (standard size name or dimensions plus
      // zero margins) is selected.

      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      // Move "if" for custom size parameters here 
      //if (ippGetValueTag(x_dim) == IPP_TAG_RANGE ||
      //	 ippGetValueTag(y_dim) == IPP_TAG_RANGE) {
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0),
				    ippGetInteger(y_dim, 0));
      name        = ippFindAttribute(media_col, "media-size-name",
				     IPP_TAG_ZERO);
      pwg_by_name = NULL;
      if (name)
      {
	entry_name = ippGetString(name, 0, NULL);
	if (entry_name)
	  pwg_by_name = pwgMediaForPWG(entry_name);
      }

      if (pwg || pwg_by_name)
      {
	if (!sizes && mode == CF_GEN_SIZES_DEFAULT)
	  continue;

	if (pwg)
	{
	  temp.width  = pwg->width;
	  temp.length = pwg->length;
	}
	else
	  temp.width = temp.length = 0;

	if (pwg_by_name)
	{
	  temp_by_name.width  = pwg_by_name->width;
	  temp_by_name.length = pwg_by_name->length;
	}
	else
	  temp_by_name.width = temp_by_name.length = 0;

	if ((margin = ippFindAttribute(media_col, "media-bottom-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.bottom = ippGetInteger(margin, 0);
	else
	  temp.bottom = *custom_bottom;

	if ((margin = ippFindAttribute(media_col, "media-left-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.left = ippGetInteger(margin, 0);
	else
	  temp.left = *custom_left;

	if ((margin = ippFindAttribute(media_col, "media-right-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.right = ippGetInteger(margin, 0);
	else
	  temp.right = *custom_right;

	if ((margin = ippFindAttribute(media_col, "media-top-margin",
				       IPP_TAG_INTEGER)) != NULL)
	  temp.top = ippGetInteger(margin, 0);
	else
	  temp.top = *custom_top;

	psname = (pwg_by_name ?
		  (pwg_by_name->ppd != NULL ?
		   pwg_by_name->ppd : pwg_by_name->pwg) :
		  (pwg->ppd != NULL ? pwg->ppd : pwg->pwg));
	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	    temp.top == 0)
	{
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	  borderless = 1;
	}
	else
	{
	  strlcpy(temp.media, psname, sizeof(temp.media));
	  borderless = 0;
	}

	// Check whether this size matches our search criteria
	if (mode != CF_GEN_SIZES_DEFAULT &&
	    min_border_mismatch > 0 &&
	    search_width > 0 && search_length > 0 &&
	    ((abs(search_width - temp_by_name.width) < 70 && // 2pt
	      abs(search_length - temp_by_name.length) < 70) || // 2pt
	     (abs(search_width - temp.width) < 70 && // 2pt
	      abs(search_length - temp.length) < 70))) // 2pt
	{
	  // Found size with the correct dimensions
	  int match = 0;
	  if (mode == CF_GEN_SIZES_SEARCH_BORDERLESS_ONLY &&
	      borderless == 1)
	  {
	    // We search only for borderless sizes and have found a match
	    border_mismatch = 0;
	    min_border_mismatch = 0;
	    if (media_col_entry)
	      *media_col_entry = media_col;
	    match = 1;
	  }
	  else if (mode == CF_GEN_SIZES_SEARCH)
	  {
	    // We search a size in general, borders are accepted. find the
	    // best match in terms of border size
	    border_mismatch =
	      (long long)(search_left < 0 ? 1 :
			  (abs(search_left - temp.left) + 1)) *
	      (long long)(search_bottom < 0 ? 1 :
			  (abs(search_bottom - temp.bottom) + 1)) *
	      (long long)(search_right < 0 ? 1 :
			  (abs(search_right - temp.right) + 1)) *
	      (long long)(search_top < 0 ? 1 :
			  (abs(search_top - temp.top) + 1));
	    if (border_mismatch < min_border_mismatch)
	    {
	      min_border_mismatch = border_mismatch;
	      if (media_col_entry)
		*media_col_entry = media_col;
	      match = 1;
	    }
	  }
	  if (match)
	  {
	    if (width)
	      *width = temp.width;
	    if (length)
	      *length = temp.length;
	    if (left)
	      *left = temp.left;
	    if (bottom)
	      *bottom = temp.bottom;
	    if (right)
	      *right = temp.right;
	    if (top)
	      *top = temp.top;
	    strlcpy(size_name, temp.media, IPP_MAX_NAME);
	  }
	}

	// Add size to list
	if (sizes && !cupsArrayFind(*sizes, &temp))
	  cupsArrayAdd(*sizes, &temp);

      }
      else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE ||
	       ippGetValueTag(y_dim) == IPP_TAG_RANGE)
      {
	//
	// Custom size - record the min/max values...
	//

	int lower, upper;   // Range values

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (min_width && lower < *min_width)
	  *min_width = lower;
	if (max_width && upper > *max_width)
	  *max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (min_length && lower < *min_length)
	  *min_length = lower;
	if (max_length && upper > *max_length)
	  *max_length = upper;
      }
    }
    if (min_border_mismatch < LLONG_MAX)
    {
      // If we have found a matching page size in the media-col-database
      // we stop searching
      min_border_mismatch = 0;
    }
  }
  if ((attr = ippFindAttribute(response, "media-size-supported",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      cups_size_t temp;   // Current size

      media_size  = ippGetCollection(attr, i);
      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0),
				    ippGetInteger(y_dim, 0));

      if (pwg) {
	if (!sizes && mode == CF_GEN_SIZES_DEFAULT)
	  continue;

	temp.width  = pwg->width;
	temp.length = pwg->length;
	temp.left   = *custom_left;
	temp.bottom = *custom_bottom;
	temp.right  = *custom_right;
	temp.top    = *custom_top;

	psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	    temp.top == 0)
	{
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	  borderless = 1;
	}
	else
	{
	  strlcpy(temp.media, psname, sizeof(temp.media));
	  borderless = 0;
	}

	// Check whether this size matches our search criteria
	if (mode != CF_GEN_SIZES_DEFAULT &&
	    min_border_mismatch > 0 &&
	    search_width > 0 && search_length > 0 &&
	    abs(search_width - temp.width) < 70 && // 2pt
	    abs(search_length - temp.length) < 70 ) // 2pt
	{
	  // Found size with the correct dimensions
	  if (mode != CF_GEN_SIZES_SEARCH_BORDERLESS_ONLY ||
	      borderless == 1)
	  {
	    // We accept the entry just by the size dimensions as
	    // "media-size-supported" has no per-size margin info
	    if (width)
	      *width = temp.width;
	    if (length)
	      *length = temp.length;
	    if (left)
	      *left = temp.left;
	    if (bottom)
	      *bottom = temp.bottom;
	    if (right)
	      *right = temp.right;
	    if (top)
	      *top = temp.top;
	    strlcpy(size_name, temp.media, IPP_MAX_NAME);
	    // Found it, stop searching
	    min_border_mismatch = 0;
	  }
	}

	if (sizes && !cupsArrayFind(*sizes, &temp))
	  cupsArrayAdd(*sizes, &temp);
      }
      else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE ||
	       ippGetValueTag(y_dim) == IPP_TAG_RANGE)
      {
	//
	// Custom size - record the min/max values...
	//

	int lower, upper;   // Range values

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (min_width && lower < *min_width)
	  *min_width = lower;
	if (max_width && upper > *max_width)
	  *max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (min_length && lower < *min_length)
	  *min_length = lower;
	if (max_length && upper > *max_length)
	  *max_length = upper;
      }
    }
  }
  if ((attr = ippFindAttribute(response, "media-supported", IPP_TAG_ZERO))
      != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char  *pwg_size = ippGetString(attr, i, NULL);
      // PWG size name
      cups_size_t temp, *temp2; // Current size, found size

      if ((pwg = pwgMediaForPWG(pwg_size)) != NULL)
      {
        if (strstr(pwg_size, "_max_") || strstr(pwg_size, "_max."))
	{
          if (max_width && pwg->width > *max_width)
            *max_width = pwg->width;
          if (max_length && pwg->length > *max_length)
            *max_length = pwg->length;
        }
	else if (strstr(pwg_size, "_min_") || strstr(pwg_size, "_min."))
	{
          if (min_width && pwg->width < *min_width)
            *min_width = pwg->width;
          if (min_length && pwg->length < *min_length)
            *min_length = pwg->length;
        }
	else
	{
	  if (!sizes && mode == CF_GEN_SIZES_DEFAULT)
	    continue;

	  temp.width  = pwg->width;
	  temp.length = pwg->length;
	  temp.left   = *custom_left;
	  temp.bottom = *custom_bottom;
	  temp.right  = *custom_right;
	  temp.top    = *custom_top;

	  psname = (pwg->ppd != NULL ? pwg->ppd : pwg->pwg);
	  if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 &&
	      temp.top == 0)
	    snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	  else
	    strlcpy(temp.media, psname, sizeof(temp.media));

	  // Add the printer's original IPP name to an already found size
	  if (sizes)
	  {
	    if ((temp2 = cupsArrayFind(*sizes, &temp)) != NULL)
	    {
	      snprintf(temp2->media + strlen(temp2->media),
		       sizeof(temp2->media) - strlen(temp2->media),
		       " %s", pwg_size);
	      // Check if we have also a borderless version of the size and add
	      // the original IPP name also there
	      snprintf(temp.media, sizeof(temp.media), "%s.Borderless", psname);
	      if ((temp2 = cupsArrayFind(*sizes, &temp)) != NULL)
		snprintf(temp2->media + strlen(temp2->media),
			 sizeof(temp2->media) - strlen(temp2->media),
			 " %s", pwg_size);
	    } else
	      cupsArrayAdd(*sizes, &temp);
	  }
	}
      }
    }
  }
  if (mode != CF_GEN_SIZES_DEFAULT && min_border_mismatch > 0 &&
      search_width > 0 && search_length > 0 &&
      *min_width >= 0 && *min_length >= 0 &&
      *max_width >= *min_width && *max_length >= *min_length &&
      *custom_left >= 0 && *custom_bottom >= 0 &&
      *custom_right >= 0 && *custom_top >= 0)
  {
    // Do we have support for a custom page size and have valid size ranges for
    // it? Check whether the size we are searching for can go as custom size
    if (search_width >= *min_width - 70 && // 2pt
	search_width <= *max_width + 70 && // 2pt
	search_length >= *min_length - 70 && // 2pt
	search_length <= *max_length + 70) // 2pt
    {
      if (width)
	*width = (search_width < *min_width ? *min_width :
		  (search_width > *max_width ? *max_width : search_width));
      if (length)
	*length = (search_length < *min_length ? *min_length :
		   (search_length > *max_length ? *max_length : search_length));
      if (left) *left = *custom_left;
      if (bottom) *bottom = *custom_bottom;
      if (right) *right = *custom_right;
      if (top) *top = *custom_top;
      min_border_mismatch = 0;
    }
  }
  if (mode != CF_GEN_SIZES_DEFAULT && min_border_mismatch > 0)
  {
    // Size not found
    if (width) *width = 0;
    if (length) *length = 0;
    if (left) *left = -1;
    if (bottom) *bottom = -1;
    if (right) *right = -1;
    if (top) *top = -1;
  }
}
