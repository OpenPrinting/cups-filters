//
// Copyright 2020 by Jai Luthra.
//

#ifndef PDFTOPDF_H
#define PDFTOPDF_H

#include <cupsfilters/filter.h>

typedef struct                                   /***** Document information *****/
{
  filter_logfunc_t logfunc;           /* Log function */
  void *logdata;                      /* Log data */
  int *JobCanceled;                   /* Set to 1 by caller */
} pdftopdf_doc_t;

#endif
