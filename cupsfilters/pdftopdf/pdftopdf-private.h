//
// Copyright 2020 by Jai Luthra.
//

#ifndef _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_H
#define _CUPS_FILTERS_PDFTOPDF_PDFTOPDF_H

#include <cupsfilters/filter.h>

typedef struct                               /***** Document information *****/
{
  cf_logfunc_t logfunc;                      /* Log function */
  void *logdata;                             /* Log data */
  cf_filter_iscanceledfunc_t iscanceledfunc; /* Function returning 1 when
                                                job is canceled, NULL for not
                                                supporting stop on cancel */
  void *iscanceleddata;                      /* User data for is-canceled
						function, can be NULL */
} pdftopdf_doc_t;

#endif
