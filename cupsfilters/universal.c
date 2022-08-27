#include "config.h"
#include "filter.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <cups/cups.h>

int                            /* O - Error status */
cfFilterUniversal(int inputfd,         /* I - File descriptor input stream */
	  int outputfd,        /* I - File descriptor output stream */
	  int inputseekable,   /* I - Is input stream seekable? */
	  cf_filter_data_t *data, /* I - Job and printer data */
	  void *parameters)    /* I - Filter-specific parameters
				      (input/output format) */
{
  char *input;
  char *final_output;
  char output[256];
  char input_super[16];
  char input_type[256];
  char output_super[16];
  char output_type[256];
  cf_filter_out_format_t *outformat;
  cf_filter_filter_in_chain_t *filter, *next;
  cf_filter_universal_parameter_t *universal_parameters;
  cf_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  int ret = 0;

  universal_parameters = (cf_filter_universal_parameter_t *)parameters;
  input = data->content_type;
  if (input == NULL)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterUniversal: No input data format supplied.");
    return (1);
  }

  final_output = data->final_content_type;
  if (final_output == NULL)
  {
    final_output = universal_parameters->actual_output_type;
    if (final_output == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterUniversal: No output data format supplied.");
      return (1);
    }
  }

  if (universal_parameters->actual_output_type)
    strncpy(output, universal_parameters->actual_output_type,
	    sizeof(output) - 1);
  else
    strncpy(output, data->final_content_type, sizeof(output) - 1);

  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterUniversal: Converting from %s to %s", input, output);
  if (log) log(ld, CF_LOGLEVEL_DEBUG,
	       "cfFilterUniversal: Final output format for job: %s",
	       final_output);

  sscanf(input, "%15[^/]/%255s", input_super, input_type);
  sscanf(output, "%15[^/]/%255s", output_super, output_type);

  cups_array_t *filter_chain;
  filter_chain = cupsArrayNew(NULL, NULL);

  if (!strcasecmp(input_super, "image") && strcasecmp(input_type, "urf") &&
      strcasecmp(input_type, "pwg-raster"))
  {
    if (!strcasecmp(output_type, "vnd.cups-raster") ||
	!strcasecmp(output_type, "urf") ||
	!strcasecmp(output_type, "pwg-raster") ||
	!strcasecmp(output_type, "PCLm"))
    {
      outformat = malloc(sizeof(cf_filter_out_format_t));
      *outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
      if (!strcasecmp(output_type, "pwg-raster") ||
	  (!strcasecmp(output_type, "vnd.cups-raster") &&
	   !strcasecmp(final_output, "image/pwg-raster")))
	*outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
      else if (!strcasecmp(output_type, "urf") ||
	       (!strcasecmp(output_type, "vnd.cups-raster") &&
		!strcasecmp(final_output, "image/urf")))
	*outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
      else if (!strcasecmp(output_type, "PCLm") ||
	       (!strcasecmp(output_type, "vnd.cups-raster") &&
		!strcasecmp(final_output, "applicationn/PCLm")))
	*outformat = CF_FILTER_OUT_FORMAT_PCLM;
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterImageToRaster;
      filter->parameters = outformat;
      filter->name = "imagetoraster";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);

      if (!strcasecmp(output, "image/pwg-raster"))
      {
	filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	outformat = malloc(sizeof(cf_filter_out_format_t));
	*outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
	filter->function = cfFilterRasterToPWG;
	filter->parameters = outformat;
	filter->name = "rastertopwg";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterUniversal: Adding %s to chain", filter->name);
      }
      else if (!strcasecmp(output, "application/PCLm"))
      {
	outformat = malloc(sizeof(cf_filter_out_format_t));
	*outformat = CF_FILTER_OUT_FORMAT_PCLM;
	filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	filter->function = cfFilterRasterToPDF;
	filter->parameters = outformat;
	filter->name = "rastertopclm";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterUniversal: Adding %s to chain", filter->name);
      }
      else if (!strcasecmp(output, "image/urf"))
      {
	filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	outformat = malloc(sizeof(cf_filter_out_format_t));
	*outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
	filter->function = cfFilterRasterToPWG;
	filter->parameters = outformat;
	filter->name = "rastertopwg";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterUniversal: Adding %s to chain", filter->name);
      }
    }
    else
    {
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterImageToPDF;
      filter->parameters = NULL;
      filter->name = "imagetopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);
    }
  }
  else
  {
    if (!strcasecmp(input, "application/postscript"))
    {
      outformat = malloc(sizeof(cf_filter_out_format_t));
      *outformat = CF_FILTER_OUT_FORMAT_PDF;
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterGhostscript;
      filter->parameters = outformat;
      filter->name = "ghostscript";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);
    }
    else if (!strcasecmp(input_super, "text") ||
	     (!strcasecmp(input_super, "application") && input_type[0] == 'x'))
    {
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      cf_filter_texttopdf_parameter_t* tparameters =
	(cf_filter_texttopdf_parameter_t *) malloc(sizeof(cf_filter_texttopdf_parameter_t));
      *tparameters = universal_parameters->texttopdf_params;
      filter->function = cfFilterTextToPDF;
      filter->parameters = tparameters;
      filter->name = "texttopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain",
		   filter->name);
    }
    else if (!strcasecmp(input, "image/urf") ||
	     !strcasecmp(input, "image/pwg-raster") ||
	     !strcasecmp(input, "application/vnd.cups-raster"))
    {
      outformat = malloc(sizeof(cf_filter_out_format_t));
      *outformat = CF_FILTER_OUT_FORMAT_PDF;
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterRasterToPDF;
      filter->parameters = outformat;
      filter->name = "rastertopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);
    }
    else if (!strcasecmp(input_type, "vnd.adobe-reader-postscript"))
    {
      outformat = malloc(sizeof(cf_filter_out_format_t));
      *outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
      if (!strcasecmp(output_type, "pwg-raster"))
	*outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
      else if(!strcasecmp(output_type, "urf"))
	*outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
      else if(!strcasecmp(output_type, "PCLm"))
	*outformat = CF_FILTER_OUT_FORMAT_PCLM;
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterGhostscript;
      filter->parameters = outformat;
      filter->name = "ghostscript";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);

      if (strcasecmp(output_type, "pwg-raster") &&
	  strcasecmp(output_type, "vnd.cups-raster") &&
	  strcasecmp(output_type, "PCLm"))
      {
	outformat = malloc(sizeof(cf_filter_out_format_t));
	*outformat = CF_FILTER_OUT_FORMAT_PDF;
	filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	filter->function = cfFilterRasterToPDF;
	filter->parameters = outformat;
	filter->name = "rastertopdf";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterUniversal: Adding %s to chain", filter->name);
      }
    }
    else if (!strcasecmp(input, "application/vnd.cups-pdf-banner"))
    {
      filter = malloc(sizeof(cf_filter_filter_in_chain_t));
      filter->function = cfFilterBannerToPDF;
      filter->parameters = NULL;
      filter->name = "bannertopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterUniversal: Adding %s to chain", filter->name);
    }
    else if (!strcasestr(input_type, "pdf"))
    {
      // Input format is not PDF and unknown -> Error
      ret = 1;
      goto out;
    }
  }
  if (((strcasecmp(input_super, "image") &&
	strcasecmp(input_type, "vnd.adobe-reader-postscript")) ||
       (strcasecmp(output_type, "vnd.cups-raster") &&
	strcasecmp(output_type, "urf") && strcasecmp(output_type, "pwg-raster") &&
	strcasecmp(output_type, "PCLm")) ||
       !strcasecmp(input_type, "urf") ||
       !strcasecmp(input_type, "pwg-raster")))
  {
    if (strcasecmp(output_type, "pdf")) {
      if (strcasecmp(input_type, "vnd.cups-pdf"))
      {
	filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	filter->function = cfFilterPDFToPDF;
	filter->parameters = strdup(output);
	filter->name = "pdftopdf";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterUniversal: Adding %s to chain", filter->name);
      }

      if (strcasecmp(output_type, "vnd.cups-pdf"))
      {
	if (!strcasecmp(output_type, "vnd.cups-raster") ||
	    !strcasecmp(output_type, "urf") ||
	    !strcasecmp(output_type, "pwg-raster") ||
	    !strcasecmp(output_type, "PCLm"))
	{
	  outformat = malloc(sizeof(cf_filter_out_format_t));
	  *outformat = CF_FILTER_OUT_FORMAT_CUPS_RASTER;
	  if (!strcasecmp(output_type, "pwg-raster"))
	    *outformat = CF_FILTER_OUT_FORMAT_PWG_RASTER;
	  else if (!strcasecmp(output_type, "urf"))
	    *outformat = CF_FILTER_OUT_FORMAT_APPLE_RASTER;
	  else if(!strcasecmp(output_type, "PCLm"))
	    *outformat = CF_FILTER_OUT_FORMAT_PCLM;
	  filter = malloc(sizeof(cf_filter_filter_in_chain_t));
	  filter->function = cfFilterGhostscript;
	  filter->parameters = outformat;
	  filter->name = "ghostscript";
	  cupsArrayAdd(filter_chain, filter);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "cfFilterUniversal: Adding %s to chain",
		       filter->name);
	}
	else
	{
	  // Output format is not PDF and unknown -> Error
	  ret = 1;
	  goto out;
	}
      }
    }
  }

 out:

  if (ret) {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "cfFilterUniversal: Unsupported combination of input and output formats: %s -> %s",
		 input, output);
  }
  else
    /* Do the dirty work ... */
    ret = cfFilterChain(inputfd, outputfd, inputseekable, data, filter_chain);

  for (filter = (cf_filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter; filter = next)
  {
    next = (cf_filter_filter_in_chain_t *)cupsArrayNext(filter_chain);
    free(filter->parameters);
    free(filter);
  }

  return ret;
}
