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
#include <ppd/ppd.h>

int                            /* O - Error status */
universal(int inputfd,         /* I - File descriptor input stream */
	  int outputfd,        /* I - File descriptor output stream */
	  int inputseekable,   /* I - Is input stream seekable? */
	  filter_data_t *data, /* I - Job and printer data */
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
  filter_out_format_t *outformat;
  filter_filter_in_chain_t *filter, *next;
  universal_parameter_t universal_parameters;
  ppd_file_t *ppd;
  ppd_cache_t *cache;
  filter_logfunc_t log = data->logfunc;
  void *ld = data->logdata;
  int ret = 0;

  universal_parameters = *(universal_parameter_t *)parameters;
  input = universal_parameters.input_format;
  if (input == NULL)
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "universal: No input data format supplied.");
    return (1);
  }
  final_output = universal_parameters.output_format;
  if (final_output == NULL)
  {
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "universal: No output data format supplied.");
    return (1);
  }
  strncpy(output, final_output, sizeof(output) - 1);

  /* If we have a PPD, load it and its cache, so that we can access the
     "*cupsFilter(2): ..." lines */
  if (data->ppd == NULL && data->ppdfile)
    data->ppd = ppdOpenFile(data->ppdfile);
  ppd = data->ppd;
  if (ppd && !ppd->cache)
    ppd->cache = ppdCacheCreateWithPPD(ppd);
  cache = ppd ? ppd->cache : NULL;

  /* Check whether our output format (under CUPS it is taken from the
     FINAL_CONTENT_TYPE env variable) is the destination format (2nd
     word) of a "*cupsFilter2: ..." line (string has 4 words), in this
     case the specified filter (4th word) does the last step,
     converting from the input format (1st word) of the line to the
     destination format and so we only need to convert to the input
     format. In this case we need to correct our output format.

     If there is more than one line with the given output format and
     an inpout format we can produce, we select the one with the
     lowest cost value (3rd word) as this is the one which CUPS should have
     chosen for this job.

     If we have "*cupsFilter: ..." lines (without "2", string with 3
     words) we do not need to do anything special, as the input format
     specified is the FIMAL_CONTENT_TYPE which CUPS supplies to us and
     into which we have to convert. So we quit parsing if the first
     line has only 3 words, as if CUPS uses the "*cupsFilter: ..."
     lines only if there is no "*cupsFilter2: ..." line min the PPD,
     so if we encounter a line with only 3 words the other lines will
     have only 3 words, too and nothing has to be done. */

  if (ppd && ppd->num_filters && cache)
  {
    int lowest_cost = INT_MAX;
    char *filter;

    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "universal: \"*cupsFilter(2): ...\" lines in the PPD file:");

    for (filter = (char *)cupsArrayFirst(cache->filters);
	 filter;
	 filter = (char *)cupsArrayNext(cache->filters))
    {
      char buf[256];
      char *ptr,
	   *in = NULL,
	   *out = NULL,
	   *coststr = NULL;
      int cost;

      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal:    %s", filter);

      /* String of the "*cupsfilter:" or "*cupsfilter2:" line */
      strncpy(buf, filter, sizeof(buf) - 1);

      /* Separate the words */
      in = ptr = buf;
      while (*ptr && !isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      *ptr = '\0';
      ptr ++;
      while (*ptr && isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      out = ptr;
      while (*ptr && !isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      *ptr = '\0';
      ptr ++;
      while (*ptr && isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      coststr = ptr;
      if (!isdigit(*ptr)) goto error;
      while (*ptr && !isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      *ptr = '\0';
      ptr ++;
      while (*ptr && isspace(*ptr)) ptr ++;
      if (!*ptr) goto error;
      cost = atoi(coststr);

      /* Valid "*cupsFilter2: ..." line ... */
      if (/* Must be of lower cost than what we selected before */
	  cost < lowest_cost &&
	  /* Must have our FINAL_CONTENT_TYPE as output */
	  strcasecmp(out, final_output) == 0 &&
	  /* Must have as input a format we are able to produce */
	  (strcasecmp(in, "application/vnd.cups-raster") == 0 ||
	   strcasecmp(in, "application/vnd.cups-pdf") == 0 ||
	   strcasecmp(in, "application/vnd.cups-postscript") == 0 ||
	   strcasecmp(in, "application/pdf") == 0 ||
	   strcasecmp(in, "image/pwg-raster") == 0 ||
	   strcasecmp(in, "image/urf") == 0 ||
	   strcasecmp(in, "application/PCLm") == 0 ||
	   strcasecmp(in, "application/postscript") == 0))
      {
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal:       --> Selecting this line");
	/* Take the input format of the line as output format for us */
	strncpy(output, in, sizeof(output));
	/* Update the minimum cost found */
	lowest_cost = cost;
	/* We cannot find a "better" solution ... */
	if (lowest_cost == 0)
	{
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "universal:    Cost value is down to zero, stopping reading further lines");
	  break;
	}
      }

      continue;

    error:
      if (lowest_cost == INT_MAX && coststr)
      {
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: PPD uses \"*cupsFilter: ...\" lines, so we always convert to format given by FINAL_CONTENT_TYPE");
	break;
      }
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "universal: Invalid \"*cupsFilter2: ...\" line in PPD: %s",
		   filter);
    }

  }

  if (strcasecmp(output, final_output) != 0)
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "universal: Converting from %s to %s, final output will be %s",
		 input, output, final_output);
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		 "universal: Converting from %s to %s", input, output);
  }

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
      outformat = malloc(sizeof(filter_out_format_t));
      *outformat = OUTPUT_FORMAT_CUPS_RASTER;
      if (!strcmp(output_type, "pwg-raster") ||
	  (!strcmp(output_type, "vnd.cups-raster") &&
	   !strcmp(final_output, "image/pwg-raster")))
	*outformat = OUTPUT_FORMAT_PWG_RASTER;
      else if (!strcmp(output_type, "urf") ||
	       (!strcmp(output_type, "vnd.cups-raster") &&
		!strcmp(final_output, "image/urf")))
	*outformat = OUTPUT_FORMAT_APPLE_RASTER;
      else if (!strcmp(output_type, "PCLm") ||
	       (!strcmp(output_type, "vnd.cups-raster") &&
		!strcmp(final_output, "applicationn/PCLm")))
	*outformat = OUTPUT_FORMAT_PCLM;
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = imagetoraster;
      filter->parameters = outformat;
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
      *parameters = universal_parameters.texttopdf_params;
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
      else if(!strcmp(output_type, "urf") ||
	      (!strcmp(output_type, "vnd.cups-raster") &&
	       !strcmp(final_output, "image/urf")))
	*outformat = OUTPUT_FORMAT_APPLE_RASTER;
      else if(!strcmp(output_type, "PCLm"))
	*outformat = OUTPUT_FORMAT_PCLM;
      filter = malloc(sizeof(filter_filter_in_chain_t));
      filter->function = ghostscript;
      filter->parameters = outformat;
      filter->name = "ghostscript";
      cupsArrayAdd(filter_chain, filter);
      if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		   "universal: Adding %s to chain", filter->name);

      if (!strcmp(output, "image/urf"))
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
      else if (strcmp(output_type, "pwg-raster") &&
	       strcmp(output_type, "vnd.cups-raster") &&
	       strcmp(output_type, "PCLm"))
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
    else if (!strstr(input_type, "pdf"))
    {
      // Input format is not PDF and unknown -> Error
      ret = 1;
      goto out;
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
      if (strcmp(input_type, "vnd.cups-pdf"))
      {
	filter = malloc(sizeof(filter_filter_in_chain_t));
	filter->function = pdftopdf;
	filter->parameters = strdup(output);
	filter->name = "pdftopdf";
	cupsArrayAdd(filter_chain, filter);
	if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		     "universal: Adding %s to chain", filter->name);
      }

      if (strcmp(output_type, "vnd.cups-pdf"))
      {
	if (!strcmp(output_type, "vnd.cups-raster") ||
	    !strcmp(output_type, "urf") ||
	    !strcmp(output_type, "pwg-raster") ||
	    !strcmp(output_type, "PCLm"))
	{
	  outformat = malloc(sizeof(filter_out_format_t));
	  *outformat = OUTPUT_FORMAT_CUPS_RASTER;
	  if (!strcmp(output_type, "pwg-raster"))
	    *outformat = OUTPUT_FORMAT_PWG_RASTER;
	  else if (!strcmp(output_type, "urf") ||
		   (!strcmp(output_type, "vnd.cups-raster") &&
		    !strcmp(final_output, "image/urf")))
	    *outformat = OUTPUT_FORMAT_APPLE_RASTER;
	  else if(!strcmp(output_type, "PCLm"))
	    *outformat = OUTPUT_FORMAT_PCLM;
	  filter = malloc(sizeof(filter_filter_in_chain_t));
	  filter->function = ghostscript;
	  filter->parameters = outformat;
	  filter->name = "ghostscript";
	  cupsArrayAdd(filter_chain, filter);
	  if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
		       "universal: Adding %s to chain",
		       filter->name);

	  if (!strcmp(output, "image/urf"))
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
    if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		 "universal: Unsupported combination of input and output formats: %s -> %s",
		 input, output);
  }
  else
    /* Do the dirty work ... */
    ret = filterChain(inputfd, outputfd, inputseekable, data, filter_chain);

  for (filter = (filter_filter_in_chain_t *)cupsArrayFirst(filter_chain);
       filter; filter = next)
  {
    next = (filter_filter_in_chain_t *)cupsArrayNext(filter_chain);
    free(filter->parameters);
    free(filter);
  }

  return ret;
}
