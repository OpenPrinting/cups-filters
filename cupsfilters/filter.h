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

typedef enum filter_loglevel_e {        /* Log levels, same as PAPPL, similar
					   to CUPS */
 FILTER_LOGLEVEL_UNSPEC = -1,                   /* Not specified */
 FILTER_LOGLEVEL_DEBUG,                         /* Debug message */
 FILTER_LOGLEVEL_INFO,                          /* Informational message */
 FILTER_LOGLEVEL_WARN,                          /* Warning message */
 FILTER_LOGLEVEL_ERROR,                         /* Error message */
 FILTER_LOGLEVEL_FATAL,                         /* Fatal message */
 FILTER_LOGLEVEL_CONTROL                        /* Control message */
} filter_loglevel_t;

typedef void (*filter_logfunc_t)(void *data, filter_loglevel_t level, const char *message, ...);

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
} filter_data_t;

typedef int (*filter_function_t)(int inputfd, int outputfd, int inputseekable,
				 int *jobcanceled, filter_data_t *data,
				 void *parameters);

typedef enum {				 /* Possible formats for rastertopdf filter */ 
  OUTPUT_FORMAT_PDF,	/* PDF */
  OUTPUT_FORMAT_PCLM	/* PCLM */
} filter_out_format_t;


/*
 * Prototypes...
 */

extern void cups_logfunc(void *data,
			 filter_loglevel_t level,
			 const char *message,
			 ...);

extern int filterCUPSWrapper(int argc,
			     char *argv[],
			     filter_function_t filter,
			     void *parameters,
			     int *JobCanceled);

extern int pclmtoraster(int inputfd,
			int outputfd,
			int inputseekable,
			int *jobcanceled,
			filter_data_t *data,
			void *parameters);

extern int pdftopdf(int inputfd,
		  int outputfd,
		  int inputseekable,
		  int *jobcanceled,
		  filter_data_t *data,
		  void *parameters);

extern int pdftops(int inputfd,
		  int outputfd,
		  int inputseekable,
		  int *jobcanceled,
		  filter_data_t *data,
		  void *parameters);

extern int pstops(int inputfd,
		  int outputfd,
		  int inputseekable,
		  int *jobcanceled,
		  filter_data_t *data,
		  void *parameters);

extern int rastertopdf(int inputfd,
		  int outputfd,
		  int inputseekable,
		  int *jobcanceled,
		  filter_data_t *data,
		  void *parameters);

extern int rastertops(int inputfd,
		  int outputfd,
		  int inputseekable,
		  int *jobcanceled,
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

