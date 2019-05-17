/*
 *   IPP Everywhere/Apple Raster/IPP legacy PPD generator header file
 *
 *   Copyright 2016 by Till Kamppeter.
 *
 *   The PPD generator is based on the PPD generator for the CUPS
 *   "lpadmin -m everywhere" functionality in the cups/ppd-cache.c
 *   file. The copyright of this file is:
 *
 *   Copyright 2010-2016 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file. 
 */

#ifndef _CUPS_FILTERS_PPDGENERATOR_H_
#  define _CUPS_FILTERS_PPDGENERATOR_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*
 * Include necessary headers...
 */

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

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif

/*
 * Prototypes...
 */

#ifdef HAVE_CUPS_1_6

extern char ppdgenerator_msg[1024];

/* Data structure for resolution (X x Y dpi) */
typedef struct res_s {
  int x, y;
} res_t;

char            *ppdCreateFromIPP(char *buffer, size_t bufsize,
				  ipp_t *response, const char *make_model,
				  const char *pdl, int color, int duplex);
char            *ppdCreateFromIPP2(char *buffer, size_t bufsize,
				   ipp_t *response, const char *make_model,
				   const char *pdl, int color, int duplex,
				   cups_array_t* conflicts,
				   cups_array_t *sizes,char* default_pagesize,
				   const char *default_cluster_color);
int             compare_resolutions(void *resolution_a, void *resolution_b,
				    void *user_data);
res_t *         ippResolutionToRes(ipp_attribute_t *attr, int index);
cups_array_t *  resolutionArrayNew();
cups_array_t*   generate_sizes(ipp_t *response,
			       ipp_attribute_t **defattr,
			       int *min_length,
			       int* min_width,
			       int* max_length,
			       int* max_width,
			       int* bottom,
			       int* left,
			       int* right,
			       int* top,
			       char* ppdname);
#endif /* HAVE_CUPS_1_6 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_PPDGENERATOR_H_ */
