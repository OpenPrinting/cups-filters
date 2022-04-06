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

#ifndef _CUPS_FILTERS_IPP_H_
#  define _CUPS_FILTERS_IPP_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <cups/cups.h>
#include <cups/backend.h>

#define CF_GET_PRINTER_ATTRIBUTES_LOGSIZE 4 * 65536
#define CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN 8192
#define CF_GET_PRINTER_ATTRIBUTES_MAX_URI_LEN 2048

extern char cf_get_printer_attributes_log[CF_GET_PRINTER_ATTRIBUTES_LOGSIZE];

/* Enum of possible driverless options */
enum cf_driverless_support_modes_e {
  CF_DRVLESS_CHECKERR,      /* Unable to get get-printer-attributes response*/
  CF_DRVLESS_FULL,          /* Standard IPP Everywhere support, works with
			       'everywhere' model */
  CF_DRVLESS_IPP11,         /* Driverless support via IPP 1.1 request */
  CF_DRVLESS_INCOMPLETEIPP  /* Driverless support without media-col-database
			       attribute */
};

char    *cfResolveURI(const char *raw_uri);
char    *cfippfindBasedURIConverter(const char *uri ,int is_fax);
int     cfCheckDriverlessSupport(const char* uri);
ipp_t   *cfGetPrinterAttributes(const char* raw_uri,
				const char* const pattrs[],
				int pattrs_size,
				const char* const req_attrs[],
				int req_attrs_size,
				int debug);
ipp_t   *cfGetPrinterAttributes2(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug);
ipp_t   *cfGetPrinterAttributes3(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int* driverless_support);
ipp_t   *cfGetPrinterAttributes4(const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int isFax);
ipp_t   *cfGetPrinterAttributes5(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int* driverless_support,
				 int resolve_uri_type);

const char* cfIPPAttrEnumValForPrinter(ipp_t *printer_attrs,
				       ipp_t *job_attrs,
				       const char *attr_name);
int cfIPPAttrIntValForPrinter(ipp_t *printer_attrs,
			      ipp_t *job_attrs,
			      const char *attr_name,
			      int   *value);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_IPP_H_ */
