//
// Common routines to access the colord CMS framework for libcupsfilter.
//
// Copyright (c) 2011-2013, Richard Hughes
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//


#ifndef _CUPS_FILTERS_COLORD_H_
#  define _CUPS_FILTERS_COLORD_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


#include <cups/raster.h>
#include <cupsfilters/filter.h>

char **cfColordGetQualifier(cf_filter_data_t *data,
			    const char *color_space,
			    const char *media_type,
			    int x_res,
			    int y_res);
char *cfColordGetProfileForDeviceID(cf_filter_data_t *data,
				    const char *device_id,
				    const char **qualifier_tuple);
int cfColordGetInhibitForDeviceID(cf_filter_data_t *data,
				  const char *device_id);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_CUPS_FILTERS_COLORD_H_
