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

#ifndef _CUPS_FILTERS_IPP_H_
#  define _CUPS_FILTERS_IPP_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus

//
// Include necessary headers...
//

#include <config.h>

#include "filter.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#if defined(WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif // WIN32 || __EMX__

#include <cups/cups.h>
#include <cups/backend.h>
#include <cups/raster.h>

#define CF_GET_PRINTER_ATTRIBUTES_LOGSIZE 4 * 65536
#define CF_GET_PRINTER_ATTRIBUTES_MAX_OUTPUT_LEN 8192
#define CF_GET_PRINTER_ATTRIBUTES_MAX_URI_LEN 2048

extern char cf_get_printer_attributes_log[CF_GET_PRINTER_ATTRIBUTES_LOGSIZE];


//
// Types...
//

// Enum of possible driverless options
enum cf_driverless_support_modes_e
{
  CF_DRVLESS_CHECKERR,      // Unable to get get-printer-attributes response*/
  CF_DRVLESS_FULL,          // Standard IPP Everywhere support, works with
                            // 'everywhere' model
  CF_DRVLESS_IPP11,         // Driverless support via IPP 1.1 request
  CF_DRVLESS_INCOMPLETEIPP  // Driverless support without media-col-database
                            // attribute
};

// Backside orientations for duplex printing
typedef enum cf_backside_orient_e
{
  CF_BACKSIDE_MANUAL_TUMBLE,
  CF_BACKSIDE_ROTATED,
  CF_BACKSIDE_FLIPPED,
  CF_BACKSIDE_NORMAL
} cf_backside_orient_t;


// Data structure for resolution (X x Y dpi)
typedef struct cf_res_s
{
  int x, y;
} cf_res_t;

typedef enum cf_gen_sizes_mode_e
{
  CF_GEN_SIZES_DEFAULT = 0,
  CF_GEN_SIZES_SEARCH,
  CF_GEN_SIZES_SEARCH_BORDERLESS_ONLY
} cf_gen_sizes_mode_t;


//
// Prototypes...
//

char            *cfResolveURI(const char *raw_uri);
char            *cfippfindBasedURIConverter(const char *uri ,int is_fax);
int             cfCheckDriverlessSupport(const char* uri);
ipp_t           *cfGetPrinterAttributes(const char* raw_uri,
					const char* const pattrs[],
					int pattrs_size,
					const char* const req_attrs[],
					int req_attrs_size,
					int debug);
ipp_t           *cfGetPrinterAttributes2(http_t *http_printer,
					 const char* raw_uri,
					 const char* const pattrs[],
					 int pattrs_size,
					 const char* const req_attrs[],
					 int req_attrs_size,
					 int debug);
ipp_t           *cfGetPrinterAttributes3(http_t *http_printer,
					 const char* raw_uri,
					 const char* const pattrs[],
					 int pattrs_size,
					 const char* const req_attrs[],
					 int req_attrs_size,
					 int debug,
					 int* driverless_support);
ipp_t           *cfGetPrinterAttributes4(const char* raw_uri,
					 const char* const pattrs[],
					 int pattrs_size,
					 const char* const req_attrs[],
					 int req_attrs_size,
					 int debug,
					 int isFax);
ipp_t           *cfGetPrinterAttributes5(http_t *http_printer,
					 const char* raw_uri,
					 const char* const pattrs[],
					 int pattrs_size,
					 const char* const req_attrs[],
					 int req_attrs_size,
					 int debug,
					 int* driverless_support,
					 int resolve_uri_type);

const char      *cfIPPAttrEnumValForPrinter(ipp_t *printer_attrs,
					    ipp_t *job_attrs,
					    const char *attr_name);
int             cfIPPAttrIntValForPrinter(ipp_t *printer_attrs,
					  ipp_t *job_attrs,
					  const char *attr_name,
					  int   *value);
int             cfIPPAttrResolutionForPrinter(ipp_t *printer_attrs,
					      ipp_t *job_attrs,
					      const char *attr_name,
					      int *xres, int *yres);
int             cfIPPReverseOutput(ipp_t *printer_attrs, ipp_t *job_attrs);
int             cfGetBackSideOrientation(cf_filter_data_t *data);
const char      *cfGetPrintRenderIntent(cf_filter_data_t *data,
					char *ri, int ri_len);

int             cfJoinJobOptionsAndAttrs(cf_filter_data_t *data,
					 int num_options,
					 cups_option_t **options);
  
char            *cfStrFormatd(char *buf, char *bufend, double number,
			      struct lconv *loc);

int             cfCompareResolutions(void *resolution_a, void *resolution_b,
				     void *user_data);
void            *cfCopyResolution(void *resolution, void *user_data);
void            cfFreeResolution(void *resolution, void *user_data);
cf_res_t        *cfNewResolution(int x, int y);
cups_array_t    *cfNewResolutionArray();
cf_res_t        *cfIPPResToResolution(ipp_attribute_t *attr, int index);
cups_array_t    *cfIPPAttrToResolutionArray(ipp_attribute_t *attr);
int             cfJoinResolutionArrays(cups_array_t **current,
				       cups_array_t **new_arr,
				       cf_res_t **current_default,
				       cf_res_t **new_default);

int             cfGetPageDimensions(ipp_t *printer_attrs,
				    ipp_t *job_attrs,
				    int num_options,
				    cups_option_t *options,
				    cups_page_header2_t *header,
				    int transverse_fit,
				    float *width,
				    float *height,
				    float *left,
				    float *bottom,
				    float *right,
				    float *top,
				    char *name,
				    ipp_t **media_col_entry);
void            cfGenerateSizes(ipp_t *response,
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
				ipp_t **media_col_entry);


#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_IPP_H_
