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
#include <cupsfilters/ppdgenerator.h>

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

#define LOGSIZE 65536
char get_printer_attributes_log[LOGSIZE];

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
ipp_t *
get_printer_attributes(const char* raw_uri,
		       const char* const pattrs[],
		       int pattrs_size,
		       const char* const req_attrs[],
		       int req_attrs_size,
		       int debug)
{
  const char *uri;
  int uri_status, host_port, i = 0, total_attrs = 0, fallback, cap = 0;
  http_t *http_printer = NULL;
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

  /* Request printer properties via IPP, for example to
      - generate a PPD file for the printer
        (mainly driverless-capable printers)
      - generally find capabilities, options, and default settinngs,
      - printers status: Accepting jobs? Busy? With how many jobs? */

  get_printer_attributes_log[0] = '\0';
  
  /* Convert DNS-SD-service-name-based URIs to host-name-based URIs */
  uri = resolve_uri(raw_uri);
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

  if ((http_printer =
       httpConnect2 (host_name, host_port, NULL, AF_UNSPEC, 
		     HTTP_ENCRYPT_IF_REQUESTED, 1, 3000, NULL)) == NULL) {
    log_printf(get_printer_attributes_log,
	       "get-printer-attributes: Cannot connect to printer with URI %s.\n",
	       uri);
    return NULL;
  }

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
		   "Full list of all IPP atttributes:\n");
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
	httpClose(http_printer);
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
    } else if (cap && fallback == 1) {
      log_printf(get_printer_attributes_log,
		 "The server doesn't support the standard IPP request, trying request without media-col\n");
    } else if (fallback == 0) {
      log_printf(get_printer_attributes_log,
		 "The server doesn't support IPP2.0 request, trying IPP1.1 request\n");
    }
  }

  httpClose(http_printer);
  return NULL;
}
#endif /* HAVE_CUPS_1_6 */
