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

#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

#define LOGSIZE 4 * 65536
#define MAX_OUTPUT_LEN 8192
#define MAX_URI_LEN 2048

char get_printer_attributes_log[LOGSIZE];

char     *resolve_uri(const char *raw_uri);
char     *ippfind_based_uri_converter(const char *uri ,int is_fax);
#ifdef HAVE_CUPS_1_6
                                /* Enum of possible driverless options */
enum driverless_support_modes {
  DRVLESS_CHECKERR,             /* Unable to get get-printer-attributes response*/
  FULL_DRVLESS,                 /* Standard IPP Everywhere support, works with 'everywhere' model */
  DRVLESS_IPP11,                /* Driverless support via IPP 1.1 request */
  DRVLESS_INCOMPLETEIPP         /* Driverless support without media-col-database attribute */
};

int check_driverless_support(const char* uri);
ipp_t   *get_printer_attributes(const char* raw_uri,
				const char* const pattrs[],
				int pattrs_size,
				const char* const req_attrs[],
				int req_attrs_size,
				int debug);
ipp_t   *get_printer_attributes2(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug);
ipp_t   *get_printer_attributes3(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int* driverless_support);
ipp_t   *get_printer_attributes4(const char* raw_uri,
				const char* const pattrs[],
				int pattrs_size,
				const char* const req_attrs[],
				int req_attrs_size,
				int debug,
        int isFax);
ipp_t   *get_printer_attributes5(http_t *http_printer,
				 const char* raw_uri,
				 const char* const pattrs[],
				 int pattrs_size,
				 const char* const req_attrs[],
				 int req_attrs_size,
				 int debug,
				 int* driverless_support,
         		 int resolve_uri_type);


#endif /* HAVE_CUPS_1_6 */

const char* ippAttrEnumValForPrinter(ipp_t *printer_attrs,
				     ipp_t *job_attrs,
				     const char *attr_name);
int ippAttrIntValForPrinter(ipp_t *printer_attrs,
			    ipp_t *job_attrs,
			    const char *attr_name,
			    int   *value);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_IPP_H_ */
