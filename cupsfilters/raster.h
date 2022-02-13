/*
 *   CUPS/PWG Raster utilities header file for CUPS.
 *
 *   Copyright 2013 by Till Kamppeter.
 *
 *   Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_RASTER_H_
#  define _CUPS_FILTERS_RASTER_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*
 * Include necessary headers...
 */

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
#  endif /* WIN32 || __EMX__ */

#  include <cups/cups.h>
#  include <cups/raster.h>

/*
 * Types
 */

typedef enum backside_orient_e
{
  BACKSIDE_MANUAL_TUMBLE,
  BACKSIDE_ROTATED,
  BACKSIDE_FLIPPED,
  BACKSIDE_NORMAL
} backside_orient_t;


/*
 * Prototypes...
 */

extern int              cupsRasterPrepareHeader(cups_page_header2_t *h,
						filter_data_t *data,
						filter_out_format_t
						final_outformat,
						filter_out_format_t
						header_outformat,
						cups_cspace_t *cspace);
extern int              cupsRasterSetColorSpace(cups_page_header2_t *h,
						const char *available,
						const char *color_mode,
						cups_cspace_t *cspace,
						int *high_depth);
extern int              cupsRasterParseIPPOptions(cups_page_header2_t *h,
						  filter_data_t *data,
						  int pwg_raster,
						  int set_defaults);
extern int		joinJobOptionsAndAttrs(filter_data_t *data,
					       int num_options,
					       cups_option_t **options);
extern int		ippRasterMatchIPPSize(cups_page_header2_t *header,
					      filter_data_t *data,
					      double margins[4],
					      double dimensions[2],
					      int *image_fit,
					      int *landscape);
extern int		getBackSideAndHeaderDuplex(ipp_t *printer_attrs,
						  cups_page_header2_t *header);

extern int 		getPrintRenderIntent(filter_data_t *data,
				cups_page_header2_t *header);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_RASTER_H_ */

/*
 * End
 */

