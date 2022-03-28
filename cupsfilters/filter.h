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
  char *printer;             /* Print queue name or NULL */
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
  int back_pipe[2];          /* File descriptors of backchannel pipe */
  int side_pipe[2];          /* File descriptors of sidechannel pipe */
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

typedef enum filter_out_format_e { /* Possible output formats for filter
				      functions */
  OUTPUT_FORMAT_PDF,	     /* PDF */
  OUTPUT_FORMAT_PDF_IMAGE,   /* Raster-only PDF */
  OUTPUT_FORMAT_PCLM,	     /* PCLM */
  OUTPUT_FORMAT_CUPS_RASTER, /* CUPS Raster */
  OUTPUT_FORMAT_PWG_RASTER,  /* PWG Raster */
  OUTPUT_FORMAT_APPLE_RASTER,/* Apple Raster */
  OUTPUT_FORMAT_PXL          /* PCL-XL */
} filter_out_format_t;

typedef struct filter_external_cups_s { /* Parameters for the
					   filterExternalCUPS() filter
					   function */
  const char *filter;        /* Path/Name of the CUPS filter to be called by
				this filter function, required */
  int is_backend;            /* 0 if we call a filter, 1 if we call a CUPS
				backend, 2 if we call a CUPS backend in
			        device discovery mode */
  const char *device_uri;    /* Device URI when calling a CUPS Backend for
				processing a job, optional, alternatively
				DEVICE_URI environment variable can get set
				in envp */
  int num_options;           /* Extra options for the 5th command line */
  cups_option_t *options;    /* argument, options of filter_data have
                                priority, 0/NULL if none */
  char **envp;               /* Additional environment variables, the already
                                defined ones stay valid but can be overwritten
                                by these ones, NULL if none */
} filter_external_cups_t;

typedef struct filter_filter_in_chain_s { /* filter entry for CUPS array to
					     be supplied to filterChain()
					     filter function */
  filter_function_t function; /* Filter function to be called */
  void *parameters;           /* Parameters for this filter function call */
  char *name;                 /* Name/comment, only for logging */
} filter_filter_in_chain_t;

typedef struct texttopdf_parameter_s {  /* parameters container of environemnt
					   variables needed by texttopdf
					   filter function */
  char *data_dir;
  char *char_set;
  char *content_type;
  char *classification;
} texttopdf_parameter_t;

typedef struct universal_parameter_s { /* Contains input and output
					  type to be supplied to the
					  universal function, and also
					  parameters for texttopdf() */
  char *input_format;                 
  char *output_format;
  texttopdf_parameter_t texttopdf_params;
} universal_parameter_t;


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


extern int filterTee(int inputfd,
		     int outputfd,
		     int inputseekable,
		     filter_data_t *data,
		     void *parameters);

/* Parameters: Filename/path (const char *) to copy the data to */


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


extern int filterExternalCUPS(int inputfd,
			      int outputfd,
			      int inputseekable,
			      filter_data_t *data,
			      void *parameters);

/* Parameters: filter_external_cups_t*
   Path/Name of the CUPS filter to be called by this filter function,
   extra options for the 5th command line argument, and extra environment
   variables */


extern int filterOpenBackAndSidePipes(filter_data_t *data);


extern void filterCloseBackAndSidePipes(filter_data_t *data);


extern int ghostscript(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: PDF, raster-only PDF, PCLm, PostScript, CUPS Raster,
   PWG Raster, Apple Raster, PCL-XL
   Note: On the Apple Raster selection the output is actually CUPS Raster
   but information about available color spaces and depths is taken from
   the urf-supported printer IPP attribute or appropriate PPD file
   attribute. This mode is for further processing with rastertopwg. This can
   change in the future when we add Apple Raster output support to
   Ghostscript's "cups" output
   device.*/


extern int bannertopdf(int inputfd,
		      int outputfd,
		      int inputseekable,
		      filter_data_t *data,
		      void *parameters);

/* Parameters: const char*
   Template directory: In this directory there are the PDF template files
   for the banners and test pages. CUPS uses /usr/share/cups/data/ for that.
   If you submit a PDF file with added banner instructions as input file
   the template directory is not needed as the PDF input file itself is used
   as template. */


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

/* Parameters: filter_out_format_t* Ouput format: CUPS Raster, PWG
   Raster, Apple Raster, PCLM
   Note: On the Apple Raster, PWG Raster, and PCLm selection the
   output is actually CUPS Raster but information about available
   color spaces and depths is taken from the urf-supported or
   pwg-raster-document-type-supported printer IPP attributes or
   appropriate PPD file attributes. This mode is for further
   processing with rastertopwg or rastertopclm. This can change in the
   future when we add Apple Raster and PWG Raster output support to
   this filter function. */


extern int mupdftoraster(int inputfd,
			 int outputfd,
			 int inputseekable,
			 filter_data_t *data,
			 void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: CUPS Raster, PWG Raster, Apple Raster, PCLm
   Note: With CUPS Raster, Apple Raster, or PCLm selections the output
   is actually PWG Raster but information about available color spaces
   and depths is taken from the pseudo-PostScript in the PPDs options,
   urf-supported printer IPP attribute or the appropriate PPD file
   attribute (PCLM is always sRGB 8-bit). These modes are for further
   processing with pwgtoraster or rastertopclm. This can change in the
   future when MuPDF adds further output formats. */


extern int pclmtoraster(int inputfd,
			int outputfd,
			int inputseekable,
			filter_data_t *data,
			void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: CUPS Raster, Apple Raster, or PWG Raster */


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


extern int pdftoraster(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void* parameters);

/* Parameters: filter_out_format_t*
   Ouput format: CUPS Raster, PWG Raster, Apple Raster, PCLm
   Note: With Apple Raster or PCLm selections the output is actually
   CUPS Raster but information about available color spaces and depths
   is taken from the urf-supported printer IPP attribute or the
   appropriate PPD file attribute (PCLM is always sRGB 8-bit). These
   modes are for further processing with rastertopwg or
   rastertopclm. This can change in the future when we add Apple
   Raster output support to this filter. */


extern int pstops(int inputfd,
		  int outputfd,
		  int inputseekable,
		  filter_data_t *data,
		  void *parameters);


extern int pwgtoraster(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: CUPS Raster, PWG Raster, Apple Raster, PCLm
   Note: On the PCLM selection the output is actually CUPS Raster
   but information about available color spaces and depths is taken from
   the pclm-... printer IPP attributes or appropriate PPD file
   attribute. This mode is for further processing with rastertopclm. */


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


extern int rastertopwg(int inputfd,
		       int outputfd,
		       int inputseekable,
		       filter_data_t *data,
		       void *parameters);

/* Parameters: filter_out_format_t*
   Ouput format: Apple Raster or PWG Raster */


extern int texttopdf(int inputfd,
		     int outputfd,
		     int inputseekable,
		     filter_data_t *data,
		     void *parameters);

/* Parameters: texttopdf_parameter_t*
   Data directory (fonts, charsets), charset, content type (for prettyprint),
   classification (for overprint/watermark) */


extern int texttotext(int inputfd,
		      int outputfd,
		      int inputseekable,
		      filter_data_t *data,
		      void *parameters);

extern int universal(int inputfd,
		     int outputfd,
		     int inputseekable,
		     filter_data_t *data,
		     void *parameters);

/* Parameters: universal_parameter_t
   Contains : Input_type: CONTENT_TYPE environment variable of CUPS
              Output type: FINAL_CONTENT TYPE environment variable of CUPS
              texttopdf_params: parameters for texttopdf */


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
