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

extern char get_printer_attributes_log[65535];

const char     *resolve_uri(const char *raw_uri);
#ifdef HAVE_CUPS_1_6
ipp_t   *get_printer_attributes(const char* raw_uri,
				const char* const pattrs[],
				int pattrs_size,
				const char* const req_attrs[],
				int req_attrs_size,
				int debug);
#endif /* HAVE_CUPS_1_6 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_IPP_H_ */
