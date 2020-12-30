/*
 *   Filter functions header file for cups-filters.
 *
 *   Copyright 2020 by Till Kamppeter.
 *
 *   Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 */

#ifndef _CUPS_FILTERS_FILTER_H_
#  define _CUPS_FILTERS_FILTER_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Include necessary headers...
 */

#  include "log.h"

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
#  include <ppd/ppd.h>


/*
 * Types and structures...
 */

typedef int (*filter_iscanceledfunc_t)(void *data);

typedef struct filter_data_s {
  int job_id;                /* Job ID or 0 */
  char *job_user;            /* Job user or NULL */
  char *job_title;           /* Job title or NULL */
  int copies;                /* Number of copies
				(1 if filter(s) should not treat it) */
  ipp_t *job_attrs;          /* IPP attributes passed along with the job */
  ipp_t *printer_attrs;      /* Printer capabilities in IPP format
				(what is answered to get-printer-attributes */
  int           num_options;
  cups_option_t *options;    /* Job options as key/value pairs */
  char *ppdfile;             /* PPD file name */
  ppd_file_t *ppd;           /* PPD file data */
  filter_logfunc_t logfunc;  /* Logging function, NULL for no logging */
  void *logdata;             /* User data for logging function, can be NULL */
  filter_iscanceledfunc_t iscanceledfunc; /* Function returning 1 when
					     job is canceled, NULL for not
					     supporting stop on cancel */
  void *iscanceleddata;      /* User data for is-canceled function, can be
				NULL */
} filter_data_t;

typedef int (*filter_function_t)(int inputfd, int outputfd, int inputseekable,
				 filter_data_t *data, void *parameters);

typedef enum filter_out_format_e { /* Possible output formats for rastertopdf()
				      filter function */
  OUTPUT_FORMAT_PDF,	     /* PDF */
  OUTPUT_FORMAT_PCLM,	     /* PCLM */
  OUTPUT_FORMAT_CUPS_RASTER, /* CUPS Raster */
  OUTPUT_FORMAT_PWG_RASTER,  /* PWG Raster */
  OUTPUT_FORMAT_PXL          /* PCL-XL */
} filter_out_format_t;

typedef struct filter_filter_in_chain_s { /* filter entry for CUPS array to
					     be supplied to filterChain()
					     filter function */
  filter_function_t function; /* Filter function to be called */
  void *parameters;           /* Parameters for this filter function call */
  char *name;                 /* Name/comment, only for logging */
} filter_filter_in_chain_t;

/*
 * Prototypes...
 */

extern void cups_logfunc(void *data,
			 filter_loglevel_t level,
			 const char *message,
			 ...);


extern int cups_iscanceledfunc(void *data);


extern int filterCUPSWrapper(int argc,
			     char *argv[],
			     filter_function_t filter,
			     void *parameters,
			     int *JobCanceled);


extern int filterPOpen(filter_function_t filter_func, /* I - Filter function */
		       int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters,
		       int *filter_pid);


extern int filterPClose(int fd,
			int filter_pid,
			filter_data_t *data);


extern int filterChain(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: Unsorted (!) CUPS array of filter_filter_in_chain_t*
   List of filters to execute in a chain, next filter takes output of
   previous filter as input, all get the same filter data, parameters
   from the array */


extern int ghostscript(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: PostScript, CUPS Raster, PWG Raster, PCL-XL */


extern int imagetopdf(int inputfd,
		      int outputfd,
		      int inputseekable,
		      filter_data_t *data,
		      void *parameters);


extern int imagetops(int inputfd,
		     int outputfd,
		     int inputseekable,
		     filter_data_t *data,
		     void *parameters);


extern int imagetoraster(int inputfd,
			 int outputfd,
			 int inputseekable,
			 filter_data_t *data,
			 void *parameters);


extern int pclmtoraster(int inputfd,
			int outputfd,
			int inputseekable,
			filter_data_t *data,
			void *parameters);


extern int pdftopdf(int inputfd,
		    int outputfd,
		    int inputseekable,
		    filter_data_t *data,
		    void *parameters);

/* Parameters: const char*
   For CUPS value of FINAL_CONTENT_TYPE environment variable, generally
   MIME type of the final output format of the filter chain for this job
   (not the output of the pdftopdf() filter function) */


extern int pdftops(int inputfd,
		   int outputfd,
		   int inputseekable,
		   filter_data_t *data,
		   void *parameters);


extern int pstops(int inputfd,
		  int outputfd,
		  int inputseekable,
		  filter_data_t *data,
		  void *parameters);


extern int rastertopdf(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: PDF, PCLm */


extern int rastertops(int inputfd,
		      int outputfd,
		      int inputseekable,
		      filter_data_t *data,
		      void *parameters);


extern void filterSetCommonOptions(ppd_file_t *ppd,
				   int num_options,
				   cups_option_t *options,
				   int change_size,
				   int *Orientation,
				   int *Duplex,
				   int *LanguageLevel,
				   int *ColorDevice,
				   float *PageLeft,
				   float *PageRight,
				   float *PageTop,
				   float *PageBottom,
				   float *PageWidth,
				   float *PageLength,
				   filter_logfunc_t log,
				   void *ld);


extern void filterUpdatePageVars(int Orientation,
				 float *PageLeft, float *PageRight,
				 float *PageTop, float *PageBottom,
				 float *PageWidth, float *PageLength);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILTERS_FILTER_H_ */

/*
 * End
 */
