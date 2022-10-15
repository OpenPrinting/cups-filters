//
//   Filter functions API definitions for libppd.
//
//   Copyright © 2020-2022 by Till Kamppeter.
//
//   Licensed under Apache License v2.0.  See the file "LICENSE" for more
//   information.
//

#ifndef _PPD_PPD_FILTER_H_
#  define _PPD_PPD_FILTER_H_

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Include necessary headers...
//

#  include <cupsfilters/log.h>
#  include <cupsfilters/filter.h>

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
#  include <ppd/ppd.h>

#  define PPD_FILTER_DATA_EXT "libppd"


//
// Types and structures...
//

typedef struct ppd_filter_data_ext_s {
  char *ppdfile;             // PPD file name
  ppd_file_t *ppd;           // PPD file data
} ppd_filter_data_ext_t;

typedef struct ppd_filter_external_cups_s { // Parameters for the
					    // ppdFilterExternalCUPS() filter
					    // function
  const char *filter;        // Path/Name of the CUPS filter to be called by
			     // this filter function, required
  int is_backend;            // 0 if we call a filter, 1 if we call a CUPS
			     // backend, 2 if we call a CUPS backend in
			     // device discovery mode
  const char *device_uri;    // Device URI when calling a CUPS Backend for
			     // processing a job, optional, alternatively
			     // DEVICE_URI environment variable can get set
			     // in envp
  int num_options;           // Extra options for the 5th command line
  cups_option_t *options;    // argument, options of filter_data have
                             // priority, 0/NULL if none
  char **envp;               // Additional environment variables, the already
                             // defined ones stay valid but can be overwritten
                             // by these ones, NULL if none
} ppd_filter_external_cups_t;


//
// Prototypes...
//

extern int ppdFilterCUPSWrapper(int argc,
				char *argv[],
				cf_filter_function_t filter,
				void *parameters,
				int *JobCanceled);


extern int ppdFilterLoadPPDFile(cf_filter_data_t *data, const char *ppdfile);


extern int ppdFilterLoadPPD(cf_filter_data_t *data);


extern void ppdFilterFreePPDFile(cf_filter_data_t *data);


extern void ppdFilterFreePPD(cf_filter_data_t *data);


extern int ppdFilterExternalCUPS(int inputfd,
				 int outputfd,
				 int inputseekable,
				 cf_filter_data_t *data,
				 void *parameters);

// Parameters: ppd_filter_external_cups_t*
// Path/Name of the CUPS filter to be called by this filter function,
// specification whether we call a filter or a backend, an in case of
// backend, whether in job processing or discovery mode, extra options
// for the 5th command line argument, and extra environment
// variables


extern int ppdFilterEmitJCL(int inputfd,
			    int outputfd,
			    int inputseekable,
			    cf_filter_data_t *data,
			    void *parameters,
			    cf_filter_function_t orig_filter);


extern int ppdFilterImageToPDF(int inputfd,
			       int outputfd,
			       int inputseekable,
			       cf_filter_data_t *data,
			       void *parameters);


extern int ppdFilterImageToPS(int inputfd,
			      int outputfd,
			      int inputseekable,
			      cf_filter_data_t *data,
			      void *parameters);


extern int ppdFilterPDFToPDF(int inputfd,
			     int outputfd,
			     int inputseekable,
			     cf_filter_data_t *data,
			     void *parameters);

// (Optional) Specification of output format via
// data->final_content_type is used for determining whether this
// filter function does page logging for CUPS (output of "PAGE: XX YY"
// log messages) or not and also to determine whether the printer or
// driver generates copies or whether we have to send the pages
// repeatedly.
//
// Alternatively, the options "pdf-filter-page-logging",
// "hardware-copies", and "hardware-collate" can be used to manually
// do these selections.


extern int ppdFilterPDFToPS(int inputfd,
			    int outputfd,
			    int inputseekable,
			    cf_filter_data_t *data,
			    void *parameters);


extern int ppdFilterPSToPS(int inputfd,
			   int outputfd,
			   int inputseekable,
			   cf_filter_data_t *data,
			   void *parameters);


extern int ppdFilterRasterToPS(int inputfd,
			       int outputfd,
			       int inputseekable,
			       cf_filter_data_t *data,
			       void *parameters);


extern int ppdFilterUniversal(int inputfd,
			      int outputfd,
			      int inputseekable,
			      cf_filter_data_t *data,
			      void *parameters);

// Requires specification of input format via data->content_type and 
// job's final output format via data->final_content_type
//
//   Parameters: cf_filter_universal_parameter_t
//
//   Contains: actual_output_type: Format which the filter should
//             actually produce if different from job's final output
//             format, or NULL to auto-determine the needed output
//	       format from the PPDs "cupsFilter2: ..." lines. Default
//	       is the job's final output format.
//	       texttopdf_params: parameters for texttopdf


extern void ppdFilterSetCommonOptions(ppd_file_t *ppd,
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
				      cf_logfunc_t log,
				      void *ld);


extern void ppdFilterUpdatePageVars(int Orientation,
				    float *PageLeft, float *PageRight,
				    float *PageTop, float *PageBottom,
				    float *PageWidth, float *PageLength);


#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_PPD_PPD_FILTER_H_
