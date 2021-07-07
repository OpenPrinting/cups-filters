/*
 * CUPS raster to PWG raster format filter for CUPS.
 *
 * Copyright © 2011, 2014-2017 Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "filter.h"
#include <ppd/ppd.h>
#include <cups/raster.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>


/*
 * 'main()' - Main entry for filter.
 */

int					/* O - Exit status */
rastertopwg(int inputfd,         /* I - File descriptor input stream */
       int outputfd,        /* I - File descriptor output stream */
       int inputseekable,   /* I - Is input stream seekable? (unused) */
       filter_data_t *data, /* I - Job and printer data */
       void *parameters)    /* I - Filter-specific parameters (unused) */
{			/* I - Command-line arguments */
cups_file_t	         *inputfp;		/* Print file */
  FILE                 *outputfp;   /* Output data stream */

  cups_raster_t		*inras;		/* Input raster stream */
			cups_raster_t *outras;	/* Output raster stream */
  cups_page_header2_t	inheader,	/* Input raster page header */
			outheader;	/* Output raster page header */
  unsigned		y;		/* Current line */
  unsigned char		*line;		/* Line buffer */
  unsigned		page = 0,	/* Current page */
			page_width,	/* Actual page width */
			page_height,	/* Actual page height */
			page_top,	/* Top margin */
			page_bottom,	/* Bottom margin */
			page_left,	/* Left margin */
			page_right,	/* Right margin */
			linesize,	/* Bytes per line */
			lineoffset;	/* Offset into line */
  int			tmp;
  unsigned char		white;		/* White pixel */
	/* PPD file */
  ppd_attr_t		*back;		/* cupsBackSide attribute */
  ppd_cache_t		*cache;		/* PPD cache */
  pwg_size_t		*pwg_size;	/* PWG media size */
  pwg_media_t		*pwg_media;	/* PWG media name */
	/* Number of options */
  cups_option_t		*options = NULL;/* Options */
  const char		*val;		/* Option value */
  filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void                 *icd = data->iscanceleddata;
  filter_logfunc_t     log = data->logfunc;
  void          *ld = data->logdata;
  filter_out_format_t output_format;
/*
  * Open the input data stream specified by inputfd ...
  */

  if ((inputfp = cupsFileOpenFd(inputfd, "r")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "rastertopwg: Unable to open input data stream.");
    }

    return (1);
  }

 /*
  * Open the output data stream specified by the outputfd...
  */

  if ((outputfp = fdopen(outputfd, "w")) == NULL)
  {
    if (!iscanceled || !iscanceled(icd))
    {
      if (log) log(ld, FILTER_LOGLEVEL_ERROR,
		   "rastertopwg: Unable to open output data stream.");
    }

    cupsFileClose(inputfp);

    return (1);
  }

  inras  = cupsRasterOpen(inputfd, CUPS_RASTER_READ);

  if (parameters){
    output_format = *(filter_out_format_t *)parameters;
      if(output_format == OUTPUT_FORMAT_PWG_RASTER)
        outras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE_PWG);
      else outras = cupsRasterOpen( outputfd, CUPS_RASTER_WRITE_APPLE);
  } else{
    if(log) log(ld, FILTER_LOGLEVEL_ERROR, " rastertopwg: Output format not specified.");
    exit(1);
  }

  if (data->ppd == NULL && data->ppdfile)
    data->ppd = ppdOpenFile(data->ppdfile);


  back  = ppdFindAttr(data->ppd, "cupsBackSide", NULL);

  if (data->ppd)
  {
    ppdMarkDefaults(data->ppd);
    ppdMarkOptions(data->ppd, data->num_options, data->options);
  }
  else
  {
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG,
     "rastertopwg: PPD file is not specified.");
  }

  cache = data->ppd ? data->ppd->cache : NULL;

  while (cupsRasterReadHeader2(inras, &inheader))
  {
   /*
    * Show page device dictionary...
    */
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: Duplex = %d", inheader.Duplex);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: HWResolution = [ %d %d ]", inheader.HWResolution[0], inheader.HWResolution[1]);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: ImagingBoundingBox = [ %d %d %d %d ]", inheader.ImagingBoundingBox[0], inheader.ImagingBoundingBox[1], inheader.ImagingBoundingBox[2], inheader.ImagingBoundingBox[3]);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: Margins = [ %d %d ]", inheader.Margins[0], inheader.Margins[1]);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: ManualFeed = %d", inheader.ManualFeed);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: MediaPosition = %d", inheader.MediaPosition);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: NumCopies = %d", inheader.NumCopies);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: Orientation = %d", inheader.Orientation);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: PageSize = [ %d %d ]", inheader.PageSize[0], inheader.PageSize[1]);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsWidth = %d", inheader.cupsWidth);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsHeight = %d", inheader.cupsHeight);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsMediaType = %d", inheader.cupsMediaType);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsBitsPerColor = %d", inheader.cupsBitsPerColor);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsBitsPerPixel = %d", inheader.cupsBitsPerPixel);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsBytesPerLine = %d", inheader.cupsBytesPerLine);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsColorOrder = %d", inheader.cupsColorOrder);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsColorSpace = %d", inheader.cupsColorSpace);
    if (log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: cupsCompression = %d", inheader.cupsCompression);

   /*
    * Compute the real raster size...
    */

    page ++;

    if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: %d %d\n", page, inheader.NumCopies);

    page_width  = (unsigned)(inheader.cupsPageSize[0] * inheader.HWResolution[0] / 72.0);
    if (page_width < inheader.cupsWidth &&
	page_width >= inheader.cupsWidth - 1)
      page_width = (unsigned)inheader.cupsWidth;
    page_height = (unsigned)(inheader.cupsPageSize[1] * inheader.HWResolution[1] / 72.0);
    if (page_height < inheader.cupsHeight &&
	page_height >= inheader.cupsHeight - 1)
      page_height = (unsigned)inheader.cupsHeight;
    page_left   = (unsigned)(inheader.cupsImagingBBox[0] * inheader.HWResolution[0] / 72.0);
    page_bottom = (unsigned)(inheader.cupsImagingBBox[1] * inheader.HWResolution[1] / 72.0);
    tmp        = (int)(page_height - page_bottom - inheader.cupsHeight);
    if (tmp < 0 && tmp >= -1) /* Rounding error */
      page_top = 0;
    else
      page_top = (unsigned)tmp;
    tmp        = (int)(page_width - page_left - inheader.cupsWidth);
    if (tmp < 0 && tmp >= -1) /* Rounding error */
      page_right = 0;
    else
      page_right = (unsigned)tmp;
    linesize    = (page_width * inheader.cupsBitsPerPixel + 7) / 8;
    lineoffset  = page_left * inheader.cupsBitsPerPixel / 8; /* Round down */

    if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: In pixels: Width: %u  Height: %u  Left: %u  Right:  %u  Top: %u  Bottom: %u", page_width, page_height, page_left, page_right, page_top, page_bottom);
    if (page_left > page_width || page_top > page_height || page_bottom > page_height || page_right > page_width)
    {
      //_cupsLangPrintFilter(stderr, "ERROR", _("Unsupported raster data."));
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Unsupported raster data.");
      if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: Bad bottom/left/top margin on page %d.", page);
      return (1);
    }

    switch (inheader.cupsColorSpace)
    {
      case CUPS_CSPACE_W :
      case CUPS_CSPACE_RGB :
      case CUPS_CSPACE_SW :
      case CUPS_CSPACE_SRGB :
      case CUPS_CSPACE_ADOBERGB :
          white = 255;
	  break;

      case CUPS_CSPACE_K :
      case CUPS_CSPACE_CMYK :
      case CUPS_CSPACE_DEVICE1 :
      case CUPS_CSPACE_DEVICE2 :
      case CUPS_CSPACE_DEVICE3 :
      case CUPS_CSPACE_DEVICE4 :
      case CUPS_CSPACE_DEVICE5 :
      case CUPS_CSPACE_DEVICE6 :
      case CUPS_CSPACE_DEVICE7 :
      case CUPS_CSPACE_DEVICE8 :
      case CUPS_CSPACE_DEVICE9 :
      case CUPS_CSPACE_DEVICEA :
      case CUPS_CSPACE_DEVICEB :
      case CUPS_CSPACE_DEVICEC :
      case CUPS_CSPACE_DEVICED :
      case CUPS_CSPACE_DEVICEE :
      case CUPS_CSPACE_DEVICEF :
          white = 0;
	  break;

      default :
	  //_cupsLangPrintFilter(stderr, "ERROR", _("Unsupported raster data."));
    if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Unsupported raster data.");
	  if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported cupsColorSpace %d on page %d.",
	          inheader.cupsColorSpace, page);
	  return (1);
    }

    if (inheader.cupsColorOrder != CUPS_ORDER_CHUNKED)
    {
      //_cupsLangPrintFilter(stderr, "ERROR", _("Unsupported raster data."));
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Unsupported raster data.");
      if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported cupsColorOrder %d on page %d.",
              inheader.cupsColorOrder, page);
      return (1);
    }

    if (inheader.cupsBitsPerPixel != 1 &&
        inheader.cupsBitsPerColor != 8 && inheader.cupsBitsPerColor != 16)
    {
      //_cupsLangPrintFilter(stderr, "ERROR", _("Unsupported raster data."));
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Unsupported raster data.");
      if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported cupsBitsPerColor %d on page %d.",
              inheader.cupsBitsPerColor, page);
      return (1);
    }

    memcpy(&outheader, &inheader, sizeof(outheader));
    outheader.cupsWidth        = page_width;
    outheader.cupsHeight       = page_height;
    outheader.cupsBytesPerLine = linesize;

    outheader.cupsInteger[14]  = 0;	/* VendorIdentifier */
    outheader.cupsInteger[15]  = 0;	/* VendorLength */

    if ((val = cupsGetOption("print-content-optimize", data->num_options,
                             options)) != NULL)
    {
      if (!strcmp(val, "automatic"))
        strncpy(outheader.OutputType, "Automatic",
                sizeof(outheader.OutputType));
      else if (!strcmp(val, "graphics"))
        strncpy(outheader.OutputType, "Graphics", sizeof(outheader.OutputType));
      else if (!strcmp(val, "photo"))
        strncpy(outheader.OutputType, "Photo", sizeof(outheader.OutputType));
      else if (!strcmp(val, "text"))
        strncpy(outheader.OutputType, "Text", sizeof(outheader.OutputType));
      else if (!strcmp(val, "text-and-graphics"))
        strncpy(outheader.OutputType, "TextAndGraphics",
                sizeof(outheader.OutputType));
      else
      {
        if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported print-content-optimize value.");
        outheader.OutputType[0] = '\0';
      }
    }

    if ((val = cupsGetOption("print-quality", data->num_options, options)) != NULL)
    {
      unsigned quality = (unsigned)atoi(val);		/* print-quality value */

      if (quality >= IPP_QUALITY_DRAFT && quality <= IPP_QUALITY_HIGH)
	outheader.cupsInteger[8] = quality;
      else
      {
	if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported print-quality %d.", quality);
	outheader.cupsInteger[8] = 0;
      }
    }

    if ((val = cupsGetOption("print-rendering-intent", data->num_options,
                             options)) != NULL)
    {
      if (!strcmp(val, "absolute"))
        strncpy(outheader.cupsRenderingIntent, "Absolute",
                sizeof(outheader.cupsRenderingIntent));
      else if (!strcmp(val, "automatic"))
        strncpy(outheader.cupsRenderingIntent, "Automatic",
                sizeof(outheader.cupsRenderingIntent));
      else if (!strcmp(val, "perceptual"))
        strncpy(outheader.cupsRenderingIntent, "Perceptual",
                sizeof(outheader.cupsRenderingIntent));
      else if (!strcmp(val, "relative"))
        strncpy(outheader.cupsRenderingIntent, "Relative",
                sizeof(outheader.cupsRenderingIntent));
      else if (!strcmp(val, "relative-bpc"))
        strncpy(outheader.cupsRenderingIntent, "RelativeBpc",
                sizeof(outheader.cupsRenderingIntent));
      else if (!strcmp(val, "saturation"))
        strncpy(outheader.cupsRenderingIntent, "Saturation",
                sizeof(outheader.cupsRenderingIntent));
      else
      {
        if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported print-rendering-intent value.");
        outheader.cupsRenderingIntent[0] = '\0';
      }
    }

    if (inheader.cupsPageSizeName[0] && (pwg_size = ppdCacheGetSize(cache, inheader.cupsPageSizeName)) != NULL && pwg_size->map.pwg)
    {
      strncpy(outheader.cupsPageSizeName, pwg_size->map.pwg,
	      sizeof(outheader.cupsPageSizeName));
    }
    else
    {
      pwg_media = pwgMediaForSize((int)(2540.0 * inheader.cupsPageSize[0] / 72.0),
				  (int)(2540.0 * inheader.cupsPageSize[1] / 72.0));

      if (pwg_media)
        strncpy(outheader.cupsPageSizeName, pwg_media->pwg,
                sizeof(outheader.cupsPageSizeName));
      else
      {
        if(log) log(ld, FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported PageSize %.2fx%.2f.",
                inheader.cupsPageSize[0], inheader.cupsPageSize[1]);
        outheader.cupsPageSizeName[0] = '\0';
      }
    }

    if (inheader.Duplex && !(page & 1) &&
        back && strcasecmp(back->value, "Normal"))
    {
      if (strcasecmp(back->value, "Flipped"))
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;/* CrossFeedTransform */
	  outheader.cupsInteger[2] = 1;	/* FeedTransform */

	  outheader.cupsInteger[3] = page_right;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_top;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_width - page_left;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_bottom;
      					/* ImageBoxBottom */
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	/* CrossFeedTransform */
	  outheader.cupsInteger[2] = ~0U;/* FeedTransform */

	  outheader.cupsInteger[3] = page_left;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_bottom;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_top;
      					/* ImageBoxBottom */
        }
      }
      else if (strcasecmp(back->value, "ManualTumble"))
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;/* CrossFeedTransform */
	  outheader.cupsInteger[2] = ~0U;/* FeedTransform */

	  outheader.cupsInteger[3] = page_width - page_left -
	                             inheader.cupsWidth;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_bottom;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_width - page_left;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_top;
      					/* ImageBoxBottom */
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	/* CrossFeedTransform */
	  outheader.cupsInteger[2] = 1;	/* FeedTransform */

	  outheader.cupsInteger[3] = page_left;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_top;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_bottom;
      					/* ImageBoxBottom */
        }
      }
      else if (strcasecmp(back->value, "Rotated"))
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;/* CrossFeedTransform */
	  outheader.cupsInteger[2] = ~0U;/* FeedTransform */

	  outheader.cupsInteger[3] = page_right;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_bottom;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_width - page_left;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_top;
      					/* ImageBoxBottom */
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	/* CrossFeedTransform */
	  outheader.cupsInteger[2] = 1;	/* FeedTransform */

	  outheader.cupsInteger[3] = page_left;
					/* ImageBoxLeft */
	  outheader.cupsInteger[4] = page_top;
					/* ImageBoxTop */
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
      					/* ImageBoxRight */
	  outheader.cupsInteger[6] = page_height - page_bottom;
      					/* ImageBoxBottom */
        }
      }
      else
      {
       /*
        * Unsupported value...
        */

        if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unsupported cupsBackSide value.");

	outheader.cupsInteger[1] = 1;	/* CrossFeedTransform */
	outheader.cupsInteger[2] = 1;	/* FeedTransform */

	outheader.cupsInteger[3] = page_left;
					/* ImageBoxLeft */
	outheader.cupsInteger[4] = page_top;
					/* ImageBoxTop */
	outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
      					/* ImageBoxRight */
	outheader.cupsInteger[6] = page_height - page_bottom;
      					/* ImageBoxBottom */
      }
    }
    else
    {
      outheader.cupsInteger[1] = 1;	/* CrossFeedTransform */
      outheader.cupsInteger[2] = 1;	/* FeedTransform */

      outheader.cupsInteger[3] = page_left;
					/* ImageBoxLeft */
      outheader.cupsInteger[4] = page_top;
					/* ImageBoxTop */
      outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
      					/* ImageBoxRight */
      outheader.cupsInteger[6] = page_height - page_bottom;
      					/* ImageBoxBottom */
    }

    if (!cupsRasterWriteHeader2(outras, &outheader))
    {
      //_cupsLangPrintFilter(stderr, "ERROR", _("Error sending raster data."));
      if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Error sending raster data.");
      if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unable to write header for page %d.", page);
      return (1);
    }

   /*
    * Copy raster data...
    */

    if (linesize < inheader.cupsBytesPerLine)
      linesize = inheader.cupsBytesPerLine;

    if ((lineoffset + inheader.cupsBytesPerLine) > linesize)
      lineoffset = linesize - inheader.cupsBytesPerLine;

    line = malloc(linesize);

    memset(line, white, linesize);
    for (y = page_top; y > 0; y --)
      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	//_cupsLangPrintFilter(stderr, "ERROR", _("Error sending raster data."));
  if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Error sending raster data.");
	if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unable to write line %d for page %d.",
	        page_top - y + 1, page);
	return (1);
      }

    for (y = inheader.cupsHeight; y > 0; y --)
    {
      if (cupsRasterReadPixels(inras, line + lineoffset, inheader.cupsBytesPerLine) != inheader.cupsBytesPerLine)
      {
	//_cupsLangPrintFilter(stderr, "ERROR", _("Error reading raster data."));
  if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Error sending raster data.");
	if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unable to read line %d for page %d.",
	        inheader.cupsHeight - y + page_top + 1, page);
	return (1);
      }

      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	//_cupsLangPrintFilter(stderr, "ERROR", _("Error sending raster data."));
  if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Error sending raster data.");
	if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unable to write line %d for page %d.",
	        inheader.cupsHeight - y + page_top + 1, page);
	return (1);
      }
    }

    memset(line, white, linesize);
    for (y = page_bottom; y > 0; y --)
      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	//_cupsLangPrintFilter(stderr, "ERROR", _("Error sending raster data."));
  if(log) log(ld, FILTER_LOGLEVEL_ERROR, "rastertopwg: Error sending raster data.");
	if(log) log(ld,FILTER_LOGLEVEL_DEBUG, "rastertopwg: Unable to write line %d for page %d.",
	        page_bottom - y + page_top + inheader.cupsHeight + 1, page);
	return (1);
      }

    free(line);
  }

  cupsRasterClose(inras);
  if (inputfd)
    close(inputfd);

  cupsFileClose(inputfp);
  fclose(outputfp);

  cupsRasterClose(outras);

  return (0);
}
