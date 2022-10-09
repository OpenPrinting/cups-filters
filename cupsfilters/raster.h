//
//   Functions to handle CUPS/PWG Raster headers for libcupsfilters.
//
//   Copyright 2013 by Till Kamppeter.
//
//   Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.


#ifndef _CUPS_FILTERS_RASTER_H_
#  define _CUPS_FILTERS_RASTER_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Include necessary headers...
//

#  include "filter.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#  include <math.h>

#  if defined(WIN32) || defined(__EMX__)
#    include <io.h>
#  else
#    include <unistd.h>
#    include <fcntl.h>
#  endif // WIN32 || __EMX__

#  include <cups/cups.h>
#  include <cups/raster.h>


//
// Prototypes...
//

extern const char       *cfRasterColorSpaceString(cups_cspace_t cspace);
extern int              cfRasterPrepareHeader(cups_page_header2_t *h,
					      cf_filter_data_t *data,
					      cf_filter_out_format_t
					      final_outformat,
					      cf_filter_out_format_t
					      header_outformat,
					      int no_high_depth,
					      cups_cspace_t *cspace);
extern int              cfRasterSetColorSpace(cups_page_header2_t *h,
					      const char *available,
					      const char *color_mode,
					      cups_cspace_t *cspace,
					      int *high_depth);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_RASTER_H_
