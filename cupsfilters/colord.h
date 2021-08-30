/*
Copyright (c) 2011-2013, Richard Hughes

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

MIT Open Source License  -  http://www.opensource.org/

*/


#ifndef _CUPS_FILTERS_COLORD_H_
#  define _CUPS_FILTERS_COLORD_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/* Common routines for accessing the colord CMS framework */

#include <cups/raster.h>
#include <ppd/ppd.h>
#include <cupsfilters/filter.h>

char  **colord_get_qualifier_for_ppd      (ppd_file_t *ppd);
char   *colord_get_profile_for_device_id  (filter_data_t *data,
                                           const char *device_id,
                                           const char **qualifier_tuple);
int     colord_get_inhibit_for_device_id  ( filter_data_t *data,
                                            const char *device_id);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_COLORD_H_ */
