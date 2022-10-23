//
// CUPS Raster to PWG/Apple Raster format filter for libcupsfilters.
//
// Copyright © 2011, 2014-2017 Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...


#include "filter.h"
#include "raster.h"
#include "ipp.h"
#include <cups/raster.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>


//
// 'cfFilterRasterToPWG()' - Filter function to convert CUPS Raster
//                           into PWG or Apple Raster

int					 // O - Exit status
cfFilterRasterToPWG(int inputfd,         // I - File descriptor input stream
		    int outputfd,        // I - File descriptor output stream
		    int inputseekable,   // I - Is input stream seekable?
		                         //     (unused)
		    cf_filter_data_t *data, // I - Job and printer data
		    void *parameters)    // I - Filter-specific parameters
                                         //     (unused)
{
  cups_raster_t		*inras;		// Input raster stream
  cups_raster_t         *outras;	// Output raster stream
  cups_page_header2_t	inheader,	// Input raster page header
			outheader;	// Output raster page header
  unsigned		y;		// Current line
  unsigned char		*line;		// Line buffer
  unsigned		page = 0,	// Current page
			page_width,	// Actual page width
			page_height,	// Actual page height
			page_top,	// Top margin
			page_bottom,	// Bottom margin
			page_left,	// Left margin
			page_right,	// Right margin
			linesize,	// Bytes per line
			lineoffset;	// Offset into line
  int			tmp;
  unsigned char		white;		// White pixel
  int           	back;           // Back side orientation
  char                  buf[64];
  int                   width,
                        height,
                        left,
                        right,
                        bottom,
                        top;
  int			num_options = 0;// Number of options
  cups_option_t		*options = NULL;// Options
  const char		*val;		// Option value
  cf_filter_iscanceledfunc_t iscanceled = data->iscanceledfunc;
  void			*icd = data->iscanceleddata;
  cf_logfunc_t		log = data->logfunc;
  void			*ld = data->logdata;
  int			res = 0;


  val = data->final_content_type;
  if (val)
  {
    if (strcasestr(val, "pwg") || strcasestr(val, "pclm"))
      outras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE_PWG);
    else if (strcasestr(val, "urf"))
      outras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE_APPLE);
    else
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPWG: Invalid output format specified. Only PWG Raster, Apple Raster/URF, and PCLm are supported.");

      close(inputfd);
      close(outputfd);
      return (1);
    }
  }
  else
  {
    if (log) log(ld, CF_LOGLEVEL_WARN,
		 "cfFilterRasterToPWG: Output format not specified, defaulting to PWG Raster.");
    
    outras = cupsRasterOpen(outputfd, CUPS_RASTER_WRITE_PWG);
  }

  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  inras  = cupsRasterOpen(inputfd, CUPS_RASTER_READ);

  while (cupsRasterReadHeader2(inras, &inheader))
  {
    if (iscanceled && iscanceled(icd))
    {
      // Canceled
      log(ld, CF_LOGLEVEL_DEBUG,
	  "cfFilterRasterToPWG: Job canceled on input page %d", page + 1);
    }
    
    //
    // Show page device dictionary...
    //

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: Duplex = %d", inheader.Duplex);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: HWResolution = [ %d %d ]",
		 inheader.HWResolution[0], inheader.HWResolution[1]);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: ImagingBoundingBox = [ %d %d %d %d ]",
		 inheader.ImagingBoundingBox[0],
		 inheader.ImagingBoundingBox[1],
		 inheader.ImagingBoundingBox[2],
		 inheader.ImagingBoundingBox[3]);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: Margins = [ %d %d ]",
		 inheader.Margins[0], inheader.Margins[1]);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: ManualFeed = %d", inheader.ManualFeed);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: MediaPosition = %d",
		 inheader.MediaPosition);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: NumCopies = %d", inheader.NumCopies);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: Orientation = %d", inheader.Orientation);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: PageSize = [ %d %d ]",
		 inheader.PageSize[0], inheader.PageSize[1]);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsWidth = %d", inheader.cupsWidth);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsHeight = %d", inheader.cupsHeight);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsMediaType = %d",
		 inheader.cupsMediaType);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsBitsPerColor = %d",
		 inheader.cupsBitsPerColor);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsBitsPerPixel = %d",
		 inheader.cupsBitsPerPixel);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsBytesPerLine = %d",
		 inheader.cupsBytesPerLine);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsColorOrder = %d",
		 inheader.cupsColorOrder);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsColorSpace = %d",
		 inheader.cupsColorSpace);
    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: cupsCompression = %d",
		 inheader.cupsCompression);

    //
    // Compute the real raster size...
    //

    page ++;

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: %d %d\n", page, inheader.NumCopies);

    page_width  = (unsigned)(inheader.cupsPageSize[0] *
			    inheader.HWResolution[0] / 72.0);
    if (page_width < inheader.cupsWidth &&
	page_width >= inheader.cupsWidth - 1)
      page_width = (unsigned)inheader.cupsWidth;
    page_height = (unsigned)(inheader.cupsPageSize[1] *
			     inheader.HWResolution[1] / 72.0);
    if (page_height < inheader.cupsHeight &&
	page_height >= inheader.cupsHeight - 1)
      page_height = (unsigned)inheader.cupsHeight;
    page_left   = (unsigned)(inheader.cupsImagingBBox[0] *
			     inheader.HWResolution[0] / 72.0);
    page_bottom = (unsigned)(inheader.cupsImagingBBox[1] *
			     inheader.HWResolution[1] / 72.0);
    tmp        = (int)(page_height - page_bottom - inheader.cupsHeight);
    if (tmp < 0 && tmp >= -1) // Rounding error
      page_top = 0;
    else
      page_top = (unsigned)tmp;
    tmp        = (int)(page_width - page_left - inheader.cupsWidth);
    if (tmp < 0 && tmp >= -1) // Rounding error
      page_right = 0;
    else
      page_right = (unsigned)tmp;
    linesize    = (page_width * inheader.cupsBitsPerPixel + 7) / 8;
    lineoffset  = page_left * inheader.cupsBitsPerPixel / 8; // Round down

    if (log) log(ld, CF_LOGLEVEL_DEBUG,
		 "cfFilterRasterToPWG: In pixels: Width: %u  Height: %u  Left: %u  Right:  %u  Top: %u  Bottom: %u",
		 page_width, page_height,
		 page_left, page_right, page_top, page_bottom);
    if (page_left > page_width || page_top > page_height ||
	page_bottom > page_height || page_right > page_width)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPWG: Unsupported raster data.");
      if (log) log(ld, CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPWG: Bad bottom/left/top margin on page %d.", page);
      res = 1;
      goto fail;
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
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		    "cfFilterRasterToPWG: Unsupported raster data.");
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unsupported cupsColorSpace %d on page %d.",
		     inheader.cupsColorSpace, page);
	res = 1;
	goto fail;
    }

    if (inheader.cupsColorOrder != CUPS_ORDER_CHUNKED)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPWG: Unsupported raster data.");
      if (log) log(ld,CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPWG: Unsupported cupsColorOrder %d on page %d.",
		   inheader.cupsColorOrder, page);
      res = 1;
      goto fail;
    }

    if (inheader.cupsBitsPerPixel != 1 &&
        inheader.cupsBitsPerColor != 8 && inheader.cupsBitsPerColor != 16)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPWG: Unsupported raster data.");
      if (log) log(ld,CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPWG: Unsupported cupsBitsPerColor %d on page %d.",
		   inheader.cupsBitsPerColor, page);
      res = 1;
      goto fail;
    }

    memcpy(&outheader, &inheader, sizeof(outheader));
    outheader.cupsWidth        = page_width;
    outheader.cupsHeight       = page_height;
    outheader.cupsBytesPerLine = linesize;

    outheader.cupsInteger[14]  = 0;	// VendorIdentifier
    outheader.cupsInteger[15]  = 0;	// VendorLength

    if ((val = cupsGetOption("print-content-optimize", num_options,
                             options)) != NULL)
    {
      if (!strcmp(val, "automatic"))
        strncpy(outheader.OutputType, "Automatic",
                sizeof(outheader.OutputType));
      else if (!strcmp(val, "graphics") ||
	       !strcmp(val, "graphic"))
        strncpy(outheader.OutputType, "Graphics", sizeof(outheader.OutputType));
      else if (!strcmp(val, "photo"))
        strncpy(outheader.OutputType, "Photo", sizeof(outheader.OutputType));
      else if (!strcmp(val, "text"))
        strncpy(outheader.OutputType, "Text", sizeof(outheader.OutputType));
      else if (!strcmp(val, "text-and-graphics") ||
	       !strcmp(val, "text-and-graphic"))
        strncpy(outheader.OutputType, "TextAndGraphics",
                sizeof(outheader.OutputType));
      else
      {
        if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unsupported print-content-optimize value.");
        outheader.OutputType[0] = '\0';
      }
    }

    if ((val = cupsGetOption("print-quality",
			     num_options, options)) != NULL)
    {
      unsigned quality = (unsigned)atoi(val); // print-quality value

      if (quality >= IPP_QUALITY_DRAFT && quality <= IPP_QUALITY_HIGH)
	outheader.cupsInteger[8] = quality;
      else
      {
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unsupported print-quality %d.", quality);
	outheader.cupsInteger[8] = 0;
      }
    }

    // Update rendering intent with user settings or the default
    outheader.cupsRenderingIntent[0] = '\0';
    cfGetPrintRenderIntent(data, outheader.cupsRenderingIntent,
			   sizeof(outheader.cupsRenderingIntent));

    // First try to use the input page size name for the output page,
    // check whether this size is supported by theprinter
    buf[0] = '\0';
    if (inheader.cupsPageSizeName[0])
    {
      // Take only a page size name for a page size the printer actually
      // supports
      snprintf(buf, sizeof(buf), "%.63s", inheader.cupsPageSizeName);
      cfGenerateSizes(data->printer_attrs, CF_GEN_SIZES_SEARCH, NULL, NULL,
		      NULL, NULL, NULL, NULL, NULL, NULL,
		      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		      buf, NULL);
    }
    if (buf[0])
      snprintf(outheader.cupsPageSizeName,
	       sizeof(outheader.cupsPageSizeName),
	       "%.63s", buf);
    else
    {
      // No name found, find the printer's page size by the size dimensions
      // and margins
      width = (int)(2540.0 * inheader.cupsPageSize[0] / 72.0);
      height = (int)(2540.0 * inheader.cupsPageSize[1] / 72.0);
      left = (int)(2540.0 * inheader.ImagingBoundingBox[0] / 72.0);
      bottom = (int)(2540.0 * inheader.ImagingBoundingBox[1] / 72.0);
      right = width - (int)(2540.0 * inheader.ImagingBoundingBox[2] / 72.0);
      top = height - (int)(2540.0 * inheader.ImagingBoundingBox[3] / 72.0);
      cfGenerateSizes(data->printer_attrs, CF_GEN_SIZES_SEARCH, NULL, NULL,
		      &width, &height, &left, &bottom, &right, &top,
		      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		      buf, NULL);
      if (buf[0])
	snprintf(outheader.cupsPageSizeName,
		 sizeof(outheader.cupsPageSizeName),
		 "%.63s", buf);
      else
      {
        if (log) log(ld, CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Page size %.2fx%.2f not supported by printer.",
		     inheader.cupsPageSize[0], inheader.cupsPageSize[1]);
        outheader.cupsPageSizeName[0] = '\0';
      }
    }

    if (inheader.Duplex && !(page & 1) &&
	(back = cfGetBackSideOrientation(data)) > 0 &&
        (back &= 7) != CF_BACKSIDE_NORMAL)
    {
      outheader.Duplex = CUPS_TRUE;
      outheader.Tumble = inheader.Tumble;

      if (back == CF_BACKSIDE_FLIPPED)
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;// CrossFeedTransform
	  outheader.cupsInteger[2] = 1;	// FeedTransform

	  outheader.cupsInteger[3] = page_right;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_top;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_width - page_left;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_bottom;
					// ImageBoxBottom
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	// CrossFeedTransform
	  outheader.cupsInteger[2] = ~0U;// FeedTransform

	  outheader.cupsInteger[3] = page_left;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_bottom;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_top;
					// ImageBoxBottom
        }
      }
      else if (back == CF_BACKSIDE_MANUAL_TUMBLE)
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;// CrossFeedTransform
	  outheader.cupsInteger[2] = ~0U;// FeedTransform

	  outheader.cupsInteger[3] = page_width - page_left -
	                             inheader.cupsWidth;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_bottom;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_width - page_left;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_top;
					// ImageBoxBottom
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	// CrossFeedTransform
	  outheader.cupsInteger[2] = 1;	// FeedTransform

	  outheader.cupsInteger[3] = page_left;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_top;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_bottom;
					// ImageBoxBottom
        }
      }
      else if (back == CF_BACKSIDE_ROTATED)
      {
        if (inheader.Tumble)
        {
	  outheader.cupsInteger[1] = ~0U;// CrossFeedTransform
	  outheader.cupsInteger[2] = ~0U;// FeedTransform

	  outheader.cupsInteger[3] = page_right;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_bottom;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_width - page_left;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_top;
					// ImageBoxBottom
        }
        else
        {
	  outheader.cupsInteger[1] = 1;	// CrossFeedTransform
	  outheader.cupsInteger[2] = 1;	// FeedTransform

	  outheader.cupsInteger[3] = page_left;
					// ImageBoxLeft
	  outheader.cupsInteger[4] = page_top;
					// ImageBoxTop
	  outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
					// ImageBoxRight
	  outheader.cupsInteger[6] = page_height - page_bottom;
					// ImageBoxBottom
        }
      }
      else
      {
	//
	// Unsupported value...
	//

        if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unsupported back side orientation value.");

	outheader.cupsInteger[1] = 1;	// CrossFeedTransform
	outheader.cupsInteger[2] = 1;	// FeedTransform

	outheader.cupsInteger[3] = page_left;
					// ImageBoxLeft
	outheader.cupsInteger[4] = page_top;
					// ImageBoxTop
	outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
					// ImageBoxRight
	outheader.cupsInteger[6] = page_height - page_bottom;
					// ImageBoxBottom
      }
    }
    else
    {
      outheader.Duplex = inheader.Duplex;
      outheader.Tumble = inheader.Tumble;

      outheader.cupsInteger[1] = 1;	// CrossFeedTransform
      outheader.cupsInteger[2] = 1;	// FeedTransform

      outheader.cupsInteger[3] = page_left;
					// ImageBoxLeft
      outheader.cupsInteger[4] = page_top;
					// ImageBoxTop
      outheader.cupsInteger[5] = page_left + inheader.cupsWidth;
					// ImageBoxRight
      outheader.cupsInteger[6] = page_height - page_bottom;
					// ImageBoxBottom
    }

    if (!cupsRasterWriteHeader2(outras, &outheader))
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "cfFilterRasterToPWG: Error sending raster data.");
      if (log) log(ld,CF_LOGLEVEL_DEBUG,
		   "cfFilterRasterToPWG: Unable to write header for page %d.", page);
      res = 1;
      goto fail;
    }

    //
    // Copy raster data...
    //

    if (linesize < inheader.cupsBytesPerLine)
      linesize = inheader.cupsBytesPerLine;

    if ((lineoffset + inheader.cupsBytesPerLine) > linesize)
      lineoffset = linesize - inheader.cupsBytesPerLine;

    line = malloc(linesize);

    memset(line, white, linesize);
    for (y = page_top; y > 0; y --)
      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterRasterToPWG: Error sending raster data.");
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unable to write line %d for page %d.",
		     page_top - y + 1, page);
	res = 1;
	goto fail;
      }

    for (y = inheader.cupsHeight; y > 0; y --)
    {
      if (cupsRasterReadPixels(inras, line + lineoffset,
			       inheader.cupsBytesPerLine) !=
	  inheader.cupsBytesPerLine)
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterRasterToPWG: Error sending raster data.");
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unable to read line %d for page %d.",
		     inheader.cupsHeight - y + page_top + 1, page);
	res = 1;
	goto fail;
      }

      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterRasterToPWG: Error sending raster data.");
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unable to write line %d for page %d.",
		     inheader.cupsHeight - y + page_top + 1, page);
	res = 1;
	goto fail;
      }
    }

    memset(line, white, linesize);
    for (y = page_bottom; y > 0; y --)
      if (!cupsRasterWritePixels(outras, line, outheader.cupsBytesPerLine))
      {
	if (log) log(ld, CF_LOGLEVEL_ERROR,
		     "cfFilterRasterToPWG: Error sending raster data.");
	if (log) log(ld,CF_LOGLEVEL_DEBUG,
		     "cfFilterRasterToPWG: Unable to write line %d for page %d.",
		     page_bottom - y + page_top + inheader.cupsHeight + 1,
		     page);
	res = 1;
	goto fail;
      }

    free(line);
  }

 fail:

  cupsRasterClose(inras);
  close(inputfd);

  cupsRasterClose(outras);
  close(outputfd);

  cupsFreeOptions(num_options, options);

  return (res);
}
