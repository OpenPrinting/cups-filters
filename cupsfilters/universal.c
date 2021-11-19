#include "filter.h"
#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <cups/cups.h>

int                            /* O - Error status */
universal(int inputfd,         /* I - File descriptor input stream */
	  int outputfd,        /* I - File descriptor output stream */
	  int inputseekable,   /* I - Is input stream seekable? */
	  filter_data_t *data, /* I - Job and printer data */
	  void *parameters)    /* I - Filter-specific parameters
				      (input/output format) */
{
  char *input;
  char *output;
  char *input_super = malloc(16);
  char *input_type = malloc(256);
  char *output_super = malloc(16);
  char *output_type = malloc(256);
  filter_out_format_t *outformat;
  filter_filter_in_chain_t *filter, *next;
  filter_input_output_format_t input_output_format;
  filter_logfunc_t log = data->logfunc;
  void *ld = data->logdata;

  input_output_format = *(filter_input_output_format_t *)parameters;
  input = input_output_format.input_format;
  output = input_output_format.output_format;
  sscanf(input, "%15[^/]/%255s", input_super, input_type);
  sscanf(output, "%15[^/]/%255s", output_super, output_type);

  cups_array_t *filter_chain;
  filter_chain = cupsArrayNew(NULL, NULL);

  if (!strcmp(input_super, "image") && strcmp(input_type, "urf") &&
      strcmp(input_type, "pwg-raster"))
  {
    if (!strcmp(output_type, "vnd.cups-raster") ||
	!strcmp(output_type, "urf") ||
	!strcmp(output_type, "pwg-raster") ||
	!strcmp(output_type, "PCLm"))
    {
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = imagetoraster;
      filter->parameters = NULL;
      filter->name = "imagetoraster";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);

      if (!strcmp(output, "image/pwg-raster"))
      {
	filter = malloc(sizeof(filter_filter_in_chain_t));
	outformat = malloc(sizeof(filter_out_format_t));
	*outformat = OUTPUT_FORMAT_PWG_RASTER;
	filter->function = rastertopwg;
	filter->parameters = outformat;
	filter->name = "rastertopwg";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: Adding %s to chain", filter->name);
      }
      else if (!strcmp(output, "application/PCLm"))
      {
	outformat = malloc(sizeof(filter_out_format_t));
	*outformat = OUTPUT_FORMAT_PCLM;
	filter = malloc(sizeof(filter_filter_in_chain_t));
	filter->function = rastertopdf;
	filter->parameters = outformat;
	filter->name = "rastertopclm";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: Adding %s to chain", filter->name);
      }
      else if (!strcmp(output, "image/urf"))
      {
	filter = malloc(sizeof(filter_filter_in_chain_t));
	outformat = malloc(sizeof(filter_out_format_t));
	*outformat = OUTPUT_FORMAT_APPLE_RASTER;
	filter->function = rastertopwg;
	filter->parameters = outformat;
	filter->name = "rastertopwg";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: Adding %s to chain", filter->name);
      }
    }
    else
    {
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = imagetopdf;
      filter->parameters = NULL;
      filter->name = "imagetopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);
    }
  }
  else
  {
    if (!strcmp(input, "application/postscript"))
    {
      outformat = malloc(sizeof(filter_out_format_t));
      *outformat = OUTPUT_FORMAT_PDF;
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = ghostscript;
      filter->parameters = outformat;
      filter->name = "ghostscript";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);
    }
    else if (!strcmp(input_super, "text") ||
	     (!strcmp(input_super, "application") && input_type[0] == 'x'))
    {
      filter = malloc(sizeof(filter_filter_in_chain_t));
      texttopdf_parameter_t* parameters =
	(texttopdf_parameter_t *) malloc(sizeof(texttopdf_parameter_t));
      char *p;
      if ((p = getenv("CUPS_DATADIR")) != NULL)
	parameters->data_dir = p;
      else
	parameters->data_dir = CUPS_DATADIR;

      if ((p = getenv("CHARSET")) != NULL)
	parameters->char_set = p;
      else
	parameters->char_set = NULL;

      if ((p = getenv("CONTENT_TYPE")) != NULL)
	parameters->content_type = p;
      else
	parameters->content_type = NULL;

      if ((p = getenv("CLASSIFICATION")) != NULL)
	parameters->classification = p;
      else
	parameters->classification = NULL;

      filter->function = texttopdf;
      filter->parameters = parameters;
      filter->name = "texttopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain",
		   filter->name);
    }
    else if (!strcmp(input, "image/urf") ||
	     !strcmp(input, "image/pwg-raster") ||
	     !strcmp(input, "application/vnd.cups-raster"))
    {
      outformat = malloc(sizeof(filter_out_format_t));
      *outformat = OUTPUT_FORMAT_PDF;
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = rastertopdf;
      filter->parameters = outformat;
      filter->name = "rastertopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);
    }
    else if (!strcmp(input_type, "vnd.adobe-reader-postscript"))
    {
      outformat = malloc(sizeof(filter_out_format_t));
      *outformat = OUTPUT_FORMAT_CUPS_RASTER;
      if (!strcmp(output_type, "pwg-raster"))
	*outformat = OUTPUT_FORMAT_PWG_RASTER;
      else if(!strcmp(output_type, "urf"))
	*outformat = OUTPUT_FORMAT_APPLE_RASTER;
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = ghostscript;
      filter->parameters = outformat;
      filter->name = "ghostscript";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);

      if (strcmp(output_type, "urf") && strcmp(output_type, "pwg-raster") &&
	  strcmp(output_type, "vnd.cups-raster"))
      {
	outformat = malloc(sizeof(filter_out_format_t));
	*outformat = OUTPUT_FORMAT_PDF;
	filter = malloc(sizeof(filter_filter_in_chain_t));
	filter->function = rastertopdf;
	filter->parameters = outformat;
	filter->name = "rastertopdf";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: Adding %s to chain", filter->name);
      }
    }
    else if (!strcmp(input, "application/vnd.cups-pdf-banner"))
    {
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = bannertopdf;
      filter->parameters = NULL;
      filter->name = "bannertopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);
    }
  }
  if (((strcmp(input_super, "image") &&
	strcmp(input_type, "vnd.adobe-reader-postscript")) ||
       (strcmp(output_type, "vnd.cups-raster") &&
	strcmp(output_type, "urf") && strcmp(output_type, "pwg-raster") &&
	strcmp(output_type, "PCLm")) ||
       !strcmp(input_type, "urf") ||
       !strcmp(input_type, "pwg-raster")))
  {
    if (strcmp(output_type, "pdf")) {
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = pdftopdf;
      filter->parameters = strdup(output);
      filter->name = "pdftopdf";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);

      if (strcmp(output_type, "vnd.cups-pdf"))
      {
	if (!strcmp(output_type, "vnd.cups-raster") ||
	    !strcmp(output_type, "urf") ||
	    !strcmp(output_type, "pwg-raster") ||
	    !strcmp(output_type, "PCLm"))
	{
	  outformat = malloc(sizeof(filter_out_format_t));
	  *outformat = OUTPUT_FORMAT_CUPS_RASTER;
	  filter = malloc(sizeof(filter_filter_in_chain_t));
	  filter->function = ghostscript;
	  filter->parameters = outformat;
	  filter->name = "ghostscript";
	  cupsArrayAdd(filter_chain, filter);
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "universal: Adding %s to chain",
		       filter->name);

	  if (!strcmp(output, "image/pwg-raster"))
	  {
	    outformat = malloc(sizeof(filter_out_format_t));
	    filter = malloc(sizeof(filter_filter_in_chain_t));
	    *outformat = OUTPUT_FORMAT_PWG_RASTER;
	    filter->function = rastertopwg;
	    filter->parameters = outformat;
	    filter->name = "rastertopwg";
	    cupsArrayAdd(filter_chain, filter);
	    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
			 "universal: Adding %s to chain",
			 filter->name);
	  }
	  else if (!strcmp(output, "application/PCLm"))
	  {
	    outformat = malloc(sizeof(filter_out_format_t));
	    *outformat = OUTPUT_FORMAT_PCLM;
	    filter = malloc(sizeof(filter_filter_in_chain_t));
	    filter->function = rastertopdf;
	    filter->parameters = outformat;
	    filter->name = "rastertopclm";
	    cupsArrayAdd(filter_chain, filter);
	    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
			 "universal: Adding %s to chain",
			 filter->name);
	  }
	  else if (!strcmp(output, "image/urf"))
	  {
	    filter = malloc(sizeof(filter_filter_in_chain_t));
	    outformat = malloc(sizeof(filter_out_format_t));
	    *outformat = OUTPUT_FORMAT_APPLE_RASTER;
	    filter->function = rastertopwg;
	    filter->parameters = outformat;
	    filter->name = "rastertopwg";
	    cupsArrayAdd(filter_chain, filter);
	    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
			 "universal: Adding %s to chain",
			 filter->name);
	  }
	}
	else if(!strcmp(output, "application/postscript") ||
		!strcmp(output, "application/vnd.cups-postscript"))
	{
	  filter = malloc(sizeof(filter_filter_in_chain_t));
	  filter->function = pdftops;
	  filter->parameters = NULL;
	  filter->name = "pdftops";
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "universal: Adding %s to chain", filter->name);
	  cupsArrayAdd(filter_chain, filter);
	}
      }
    }
  }

  int ret = filterChain(inputfd, outputfd, inputseekable, data, filter_chain);

  free(input_super);
  free(input_type);
  free(output_super);
  free(output_type);
  for (filter = (filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter; filter = next)
  {
    next = (filter_filter_in_chain_t *)cupsArrayNext(filter_chain);
    free(filter->parameters);
    free(filter);
  }

  return ret;
}
