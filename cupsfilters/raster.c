//
//   Functions to handle CUPS/PWG Raster headers for libcupsfilters.
//
//   Copyright 2013-2022 by Till Kamppeter.
//
//   Distribution and use rights are outlined in the file "COPYING"
//   which should have been included with this file.
//
//   cfRasterColorSpaceString() - Return strings for CUPS color spaces
//   cfRasterPrepareHeader()    - Prepare a Raster header for a job
//   cfRasterSetColorSpace()    - Find best color space for print-color-mode
//                                and print-quality setting
//

//
// Include necessary headers.
//

#include <config.h>
#include <cups/cups.h>
#include "raster.h"
#include "filter.h"
#include "driver.h"
#include "ipp.h"
#include <string.h>
#include <ctype.h>
#include <cupsfilters/ipp.h>
#include <cups/pwg.h>

//
// Local functions
//

static int raster_base_header(cups_page_header2_t *h, cf_filter_data_t *data,
			      int pwg_raster);

//
// '_strlcpy()' - Safely copy two strings.
//

static size_t				// O - Length of string
_strlcpy(char        *dst,		// O - Destination string
	 const char  *src,		// I - Source string
	 size_t      size)		// I - Size of destination string buffer
{
  size_t	srclen;			// Length of source string


  //
  // Figure out how much room is needed...
  //

  size --;

  srclen = strlen(src);

  //
  // Copy the appropriate amount...
  //

  if (srclen > size)
    srclen = size;

  memcpy(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}


//
// 'cfRasterColorSpaceString()' - Return the color space name for a
//                                cupsColorSpace value.

const char *
cfRasterColorSpaceString(cups_cspace_t cspace)	// I - cupsColorSpace value
{
  static const char * const cups_color_spaces[] =
  {					// Color spaces
    "W",
    "RGB",
    "RGBA",
    "K",
    "CMY",
    "YMC",
    "CMYK",
    "YMCK",
    "KCMY",
    "KCMYcm",
    "GMCK",
    "GMCS",
    "WHITE",
    "GOLD",
    "SILVER",
    "CIEXYZ",
    "CIELab",
    "RGBW",
    "SW",
    "SRGB",
    "ADOBERGB",
    "21",
    "22",
    "23",
    "24",
    "25",
    "26",
    "27",
    "28",
    "29",
    "30",
    "31",
    "ICC1",
    "ICC2",
    "ICC3",
    "ICC4",
    "ICC5",
    "ICC6",
    "ICC7",
    "ICC8",
    "ICC9",
    "ICCA",
    "ICCB",
    "ICCC",
    "ICCD",
    "ICCE",
    "ICCF",
    "47",
    "DEVICE1",
    "DEVICE2",
    "DEVICE3",
    "DEVICE4",
    "DEVICE5",
    "DEVICE6",
    "DEVICE7",
    "DEVICE8",
    "DEVICE9",
    "DEVICEA",
    "DEVICEB",
    "DEVICEC",
    "DEVICED",
    "DEVICEE",
    "DEVICEF"
  };

  if (cspace < CUPS_CSPACE_W || cspace > CUPS_CSPACE_DEVICEF)
    return ("Unknown");
  else
    return (cups_color_spaces[cspace]);
}


//
// 'cfRasterPrepareHeader() - This function creates a CUPS/PWG Raster
//                            header for Raster output based on the
//                            printer and job properties supplied to
//                            the calling filter functions, printer
//                            properties via printer IPP attributes
//                            and job properties via CUPS option list
//                            and job IPP attributesor optionally a
//                            sample header. For PWG and Apple Raster
//                            output the color space and depth is
//                            auto-selected based on available options
//                            listed in the urf-supported and
//                            pwg-raster-document-type-supported
//                            printer IPP attributes and the settings
//                            of print-color-mode ("ColorModel") and
//                            print-quality ("cupsPrintQuality") job
//                            attributes/options.
//

int                                             // O  -  0 on success,
						//      -1 on error
cfRasterPrepareHeader(cups_page_header2_t *h,   // I  - Raster header
			cf_filter_data_t *data, // I  - Job and printer data
			cf_filter_out_format_t final_outformat,
                                                // I  - Job output format
						//      (determines color space,
						//       and resolution)
			cf_filter_out_format_t header_outformat,
                                                // I  - This filter's output
						//      format (determines
						//      header format)
			int no_high_depth,      // I  - Suppress use of
						//      > 8 bit per color
			cups_cspace_t *cspace)  // IO - Color space we want to
						//      use, -1 for auto, we
						//      return color space
						//      actually used, -1 if
						//      no suitable color space
						//      found.
{
  int i;
  ipp_t *printer_attrs, *job_attrs;
  int num_options = 0;
  cups_option_t *options = NULL;
  cf_logfunc_t log = data->logfunc;
  void         *ld = data->logdata;
  int pwgraster = 0,
      appleraster = 0,
      cupsraster = 0,
      pclm = 0;
  int cupsrasterheader = 1;
  const char *p;
  ipp_attribute_t *attr;
  const char *cspaces_available = NULL, *color_mode = NULL, *quality = NULL;
  int hi_depth;
  char valuebuffer[2048];
  int res = 1;
  int xres = -1; int yres = -1;
  float margins[4];
  float dimensions[2];
  char size_name_buf[IPP_MAX_NAME + 1];
  

  if (final_outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER)
    pwgraster = 1;
  else if (final_outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
    appleraster = 1;
  else if (final_outformat == CF_FILTER_OUT_FORMAT_PCLM)
    pclm = 1;
  else
    cupsraster = 1;

  if (cupsraster)
  {
    pwgraster = 0;
    p = cupsGetOption("media-class", num_options, options);
    if (p == NULL)
      p = cupsGetOption("MediaClass", num_options, options);
    if (p != NULL)
    {
      if (strcasestr(p, "pwg"))
      {
	pwgraster = 1;
	cupsraster = 0;
	if (log)
	  log(ld, CF_LOGLEVEL_DEBUG,
	      "PWG Raster output requested (via \"MediaClass\"/\"media-class\" option)");
      }
      else
	pwgraster = 0;
    }
  }

  if (header_outformat == CF_FILTER_OUT_FORMAT_PWG_RASTER ||
      header_outformat == CF_FILTER_OUT_FORMAT_APPLE_RASTER)
    cupsrasterheader = 0;

  printer_attrs = data->printer_attrs;
  job_attrs = data->job_attrs;

  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);
  printer_attrs = data->printer_attrs;
  job_attrs = data->job_attrs;

  // These values will be used in case we don't find supported resolutions
  // for given output format
  if ((attr = ippFindAttribute(printer_attrs, "printer-resolution-default",
			       IPP_TAG_RESOLUTION)) != NULL)
  {
    ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
    const char *p = valuebuffer;
    xres = atoi(p);
    if ((p = strchr(p, 'x')) != NULL)
      yres = atoi(p + 1);
    else
      yres = xres;
  }
  // Finding supported resolution for given output format
  if (pwgraster)
  {
    if ((attr = ippFindAttribute(printer_attrs,
				 "pwg-raster-document-resolution-supported",
				 IPP_TAG_RESOLUTION)) != NULL)
    {
      ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
      const char *p = valuebuffer;
      xres = atoi(p);
      if ((p = strchr(p, 'x')) != NULL)
        yres = atoi(p + 1);
      else
        yres = xres;
    }
  }
  else if (appleraster)
  {
    if ((attr = ippFindAttribute(printer_attrs, "urf-supported",
				 IPP_TAG_KEYWORD))!=NULL)
    {
      for (int i = 0; i < ippGetCount(attr); i ++)
      {
        const char *p = ippGetString(attr, i, NULL);
        if (strncasecmp(p, "RS", 2))
	  continue;
        int lo; int hi;
        lo = atoi(p + 2);
        if (lo == 0)
	  lo = -1;
        p = strchr(p, '-');
        if (p)
	  hi = atoi(p + 1);
        else
	  hi = lo;
        xres = hi;
        yres = hi;
      }
    }
  }
  else if (pclm)
  {
    if ((attr = ippFindAttribute(printer_attrs,
				 "pclm-source-resolution-default",
				 IPP_TAG_RESOLUTION)) != NULL)
    {
      ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
      const char *p = valuebuffer;
      xres = atoi(p);
      if ((p = strchr(p, 'x')) != NULL)
        yres = atoi(p + 1);
      else
        yres = xres;
    }
  }

  if (log)
  {
    if (*cspace == -1)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "Color space requested: Default");
    else
      log(ld, CF_LOGLEVEL_DEBUG,
	  "Color space requested: #%d", *cspace);
    log(ld, CF_LOGLEVEL_DEBUG,
	"Final output format: %s",
	appleraster ? "Apple Raster" :
	(pwgraster ? "PWG Raster" :
	 (pclm ? "PCLm" : "CUPS Raster")));
  }

  if (data->header)
    *h = *(data->header); // Copy sample header
  else
    raster_base_header(h, data, 1 - cupsrasterheader);
  if (cfGetPageDimensions(data->printer_attrs, data->job_attrs,
			  data->num_options, data->options, h, 0,
			  &(dimensions[0]), &(dimensions[1]),
			  &(margins[0]), &(margins[1]),
			  &(margins[2]), &(margins[3]), size_name_buf,
			  NULL) < 0)
  {
    pwg_media_t *pwg_media;
    
    for (i = 0; i < 2; i ++)
      dimensions[i] = h->cupsPageSize[i];
    margins[0] = h->cupsImagingBBox[0];
    margins[1] = h->cupsImagingBBox[1];
    margins[2] = dimensions[0] - h->cupsImagingBBox[2];
    margins[3] = dimensions[1] - h->cupsImagingBBox[3];
    pwg_media = pwgMediaForSize(dimensions[0] / 72 * 2540,
				dimensions[1] / 72 * 2540);
    if (pwg_media)
    {
      p = (pwg_media->ppd ? pwg_media->ppd :
	   (pwg_media->legacy ? pwg_media->legacy : pwg_media->pwg));
      if (p)
	_strlcpy(h->cupsPageSizeName, p, sizeof(h->cupsPageSizeName));
    }
  }
  else if (size_name_buf[0])
    _strlcpy(h->cupsPageSizeName, size_name_buf, sizeof(h->cupsPageSizeName));

  if (!cupsrasterheader)
    memset(margins, 0, sizeof(margins)); 

  if (printer_attrs &&
      ((pwgraster &&
	(attr = ippFindAttribute(printer_attrs,
				 "pwg-raster-document-type-supported",
				 IPP_TAG_ZERO)) != NULL) ||
       (appleraster &&
	(attr = ippFindAttribute(printer_attrs,
				 "urf-supported",
				 IPP_TAG_ZERO)) != NULL)))
  {
    ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
    cspaces_available = valuebuffer;
    if ((color_mode = cupsGetOption("print-color-mode", num_options,
				    options)) == NULL)
      color_mode = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs,
					      "print-color-mode");
    if ((quality = cupsGetOption("print-quality", num_options,
				 options)) == NULL)
      quality = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs,
					   "print-quality");
    hi_depth = (!no_high_depth && quality &&
		(!strcasecmp(quality, "high") || !strcmp(quality, "5"))) ?
      1 : 0;
    if (log)
    {
      log(ld, CF_LOGLEVEL_DEBUG,
	  "Color mode requested: %s; color depth requested: %s",
	  color_mode, hi_depth ? "High" : "Standard");
      log(ld, CF_LOGLEVEL_DEBUG,
	  "Determining best color space/depth ...");
    }
    res = cfRasterSetColorSpace(h, cspaces_available, color_mode,
				cspace, &hi_depth);
  }
  else if (pclm)
  {
    // Available color spaces are always SRGB 8 and SGray 8
    cspaces_available = "srgb_8,sgray_8";
    if ((color_mode = cupsGetOption("print-color-mode", num_options,
				    options)) == NULL)
      color_mode = cfIPPAttrEnumValForPrinter(printer_attrs, job_attrs,
					      "print-color-mode");
    hi_depth = 0;
    if (log)
      log(ld, CF_LOGLEVEL_DEBUG,
	  "For PCLm color mode is always SRGB/SGray 8-bit.");
    res = cfRasterSetColorSpace(h, cspaces_available, color_mode,
				cspace, &hi_depth);
  }

  if (res != 1)
  {
    // cfRasterSetColorSpace() was called
    if (res < 0)
    {
      // failed
      if (log)
      {
	log(ld, CF_LOGLEVEL_ERROR,
	    "Unable to set color space/depth for Raster output!");
	if (*cspace < 0)
	  log(ld, CF_LOGLEVEL_ERROR,
	      "Did not find a valid color space!");
	else
	  log(ld, CF_LOGLEVEL_ERROR,
	      "Requested color space #%d not a valid PWG/Apple Raster color space!",
	      *cspace);
      }
      return (-1);
    }
    else
      // succeeded
      if (log)
	log(ld, CF_LOGLEVEL_DEBUG,
	    "Using color space #%d with %s color depth",
	    *cspace, hi_depth ? "high" : "standard");
  }

  if ((h->HWResolution[0] == 100) && (h->HWResolution[1] == 100))
  {
    // No resolution set in header
    if (xres != -1)
    {
      h->HWResolution[0] = xres;
      h->HWResolution[1] = yres;  
    }
    else
    {
      h->HWResolution[0] = 300;
      h->HWResolution[1] = 300;
    }
    h->cupsWidth = h->HWResolution[0] * h->PageSize[0] / 72;
    h->cupsHeight = h->HWResolution[1] * h->PageSize[1] / 72;
  }

  // Make all page geometry fields in the header consistent
  if (cupsrasterheader)
  {
    h->cupsWidth = ((dimensions[0] - margins[0] - margins[2]) /
		    72.0 * h->HWResolution[0]) + 0.5;
    h->cupsHeight = ((dimensions[1] - margins[1] - margins[3]) /
		     72.0 * h->HWResolution[1]) + 0.5;
  }
  else
  {
    h->cupsWidth = (dimensions[0] /
		    72.0 * h->HWResolution[0]) + 0.5;
    h->cupsHeight = (dimensions[1] /
		     72.0 * h->HWResolution[1]) + 0.5;
  }
  for (i = 0; i < 2; i ++)
  {
    h->cupsPageSize[i] = dimensions[i];
    h->PageSize[i] = (unsigned int)(h->cupsPageSize[i] + 0.5);
    if (cupsrasterheader)
      h->Margins[i] = margins[i] + 0.5;
    else
      h->Margins[i] = 0;
  }
  if (cupsrasterheader)
  {
    h->cupsImagingBBox[0] = margins[0];
    h->cupsImagingBBox[1] = margins[1];
    h->cupsImagingBBox[2] = dimensions[0] - margins[2];
    h->cupsImagingBBox[3] = dimensions[1] - margins[3];
    for (i = 0; i < 4; i ++)
      h->ImagingBoundingBox[i] =
	(unsigned int)(h->cupsImagingBBox[i] + 0.5);
  }
  else
    for (i = 0; i < 4; i ++)
    {
      h->cupsImagingBBox[i] = 0.0;
      h->ImagingBoundingBox[i] = 0;
    }
  h->cupsBytesPerLine = (h->cupsBitsPerPixel * h->cupsWidth + 7) / 8;
  if (h->cupsColorOrder == CUPS_ORDER_BANDED)
    h->cupsBytesPerLine *= h->cupsNumColors;

  // Mark header as PWG Raster if it is not CUPS Raster
  if (!cupsrasterheader)
    strcpy(h->MediaClass, "PwgRaster");

  cupsFreeOptions(num_options, options);

  return (0);
}


//
// 'cfRasterSetColorSpace() - Update a given CUPS/PWG Raster header to
//                            the desired color mode, color space, and
//                            color depth. We supply one of the
//                            printer IPP attributes urf-supported or
//                            pwg-raster-document-type-supported as
//                            they contain a list of all valid combos
//                            of color space and color depth supported
//                            by the printer, tell the
//                            print-color-mode attribute setting for
//                            this job and request a color space and
//                            optionally high color depth. Then it is
//                            checked first whether the requested
//                            color space is available and if not we
//                            fall back to the base color space
//                            (usually sGray or sRGB). Then knowing
//                            the color space we will use, we check
//                            whether in this color space more than
//                            one color depth is supported, we chooce
//                            the lowest, and if high color depth is
//                            requested, the highest.
//

int                                             // O  -  0 on success,
						//      -1 on error
cfRasterSetColorSpace(cups_page_header2_t *h,   // I  - Raster header
			const char *available,  // I  - Available color spaces
						//      from IPP attribute
						//      urf-supported or
					   // pwg-raster-document-type-supported
			const char *color_mode, // I  - print-color-mode IPP
						//      attribute setting
			cups_cspace_t *cspace,  // IO - Color space we want to
						//      use, -1 for auto, we
						// 	return color space
						// 	actually used, -1 if
						//      no suitable color space
						//      found.
			int *high_depth)        // IO - Do we want to print in
						//      high color depth? We
						//      reset to 0 if high
						//      quality not supported
						//      in the color space
						//      used.
{
  int min_depth = 999;
  int max_depth = 0;
  int best_depth = -1;
  int num_colors;
  int cspace_fallback = 0; // 0: originally requested color space
			   // 1: sRGB for color, sGray for mono
			   // 2: sRGB for mono
  const char *p, *q;

  // Range-check
  if (!h || !available || !cspace)
    return (-1);

  if (*cspace != -1 && *cspace != CUPS_CSPACE_SW &&
      *cspace != CUPS_CSPACE_SRGB && *cspace != CUPS_CSPACE_ADOBERGB &&
      *cspace != CUPS_CSPACE_W && *cspace != CUPS_CSPACE_K &&
      *cspace != CUPS_CSPACE_RGB && *cspace != CUPS_CSPACE_CMYK)
    return (-1);

  // True Bi-Level only available in PWG Raster.
  // List of properties in pwg-raster-document-type-supported IPP attribute
  // is lower-case-only whereas urf-supported for Apple Raster is upper-case-
  // only
  if (islower(available[0]) &&
      (p = strstr(available, "black_1")) != NULL &&
      !isdigit(*(p + 7)) &&
      (!strcmp(color_mode, "bi-level") ||
       !strcmp(color_mode, "process-bi-level")))
  {
    // Set parameters for bi-level, there is only one color space and color
    // depth
    *cspace = CUPS_CSPACE_K;
    best_depth = 1;
    num_colors = 1;
    // No high color depth in bi-level
    if (high_depth)
      *high_depth = 0;
  }
  else
  {
    // Any other color space
    for (;;)
    {      // Loop through fallbacks to default if requested color space
	   // not supported
      if (*cspace != -1)
      {    // Skip if no color space specified
	for (p = available; p; p = q)
	{
	  int n, dmin, dmax;
	  // Check whether requested color space is supported
	  if ((*cspace == CUPS_CSPACE_SW &&
	       (((q = strchr(p, 'W')) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q ++) &&
		 isdigit(*q)) ||
		((q = strstr(p, "sgray_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 6) &&
		 isdigit(*q))) &&
	       (num_colors = 1)) ||
	      (*cspace == CUPS_CSPACE_W &&
	       (((q = strstr(p, "DEVW")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 4) &&
		 isdigit(*q)) ||
		((q = strstr(p, "black_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 6) &&
		 isdigit(*q))) &&
	       (num_colors = 1)) ||
	      (*cspace == CUPS_CSPACE_SRGB &&
	       (((q = strstr(p, "SRGB")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 4) &&
		 isdigit(*q)) ||
		((q = strstr(p, "srgb_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 5) &&
		 isdigit(*q))) &&
	       (num_colors = 3)) ||
	      (*cspace == CUPS_CSPACE_ADOBERGB &&
	       (((q = strstr(p, "ADOBERGB")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 8) &&
		 isdigit(*q)) ||
		((q = strstr(p, "adobe-rgb_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 10) &&
		 isdigit(*q))) &&
	       (num_colors = 3)) ||
	      (*cspace == CUPS_CSPACE_RGB &&
	       (((q = strstr(p, "DEVRGB")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 6) &&
		 isdigit(*q)) ||
		((q = strstr(p, "rgb_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 4) &&
		 isdigit(*q))) &&
	       (num_colors = 3)) ||
	      (*cspace == CUPS_CSPACE_CMYK &&
	       (((q = strstr(p, "DEVCMYK")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 7) &&
		 isdigit(*q)) ||
		((q = strstr(p, "cmyk_")) != NULL &&
		 (q == available || *(q - 1) == ',') &&
		 (q += 5) &&
		 isdigit(*q))) &&
	       (num_colors = 4)))
	  {
	    // Color space supported, check color depth values
	    n = sscanf(q, "%d-%d", &dmin, &dmax);
	    if (isupper(available[0]))
	      // urf-supported specifies bits per pixel, we need bits per
	      // color
	      dmin = dmin / num_colors;
	    if (dmin < min_depth)
	      min_depth = dmin;
	    if (n == 2)
	    {
	      if (isupper(available[0]))
		// urf-supported specifies bits per pixel, we need bits per
		// color
		dmax = dmax / num_colors;
	      if (dmax > max_depth)
		max_depth = dmax;
	    }
	    else
	    {
	      if (dmin > max_depth)
		max_depth = dmin;
	    }
	    // Select depth depending on whether we want to have high or
	    // standard color depth
	    if (high_depth && *high_depth)
	      best_depth = max_depth;
	    else
	      best_depth = min_depth;
	  }
	  else
	    // Advance to the next color space entry
	    if (q && *q != '\0')
	      q ++;
	}
	if (best_depth > 0)
	{
	  // The requested color space is supported, so quit the fallback
	  // loop
	  if (high_depth && *high_depth && min_depth == max_depth)
	    // We requested high color depth but our color space is only
	    // supported with a single color depth, reset request to tell
	    // that we did not find a higher color depth
	    *high_depth = 0;
	  break;
	}
      }
      // Arrived here, the requested color depth is not supported, try next
      // fallback level
      cspace_fallback ++;
      if (cspace_fallback > 2)
      {
	// Gone through all fallbacks and still no suitable color space?
	// Quit finally
	*cspace = -1;
	return (-1);
      }
      // Fallback 1: Suitable color space for the requested color mode
      // Fallback 2: Color always (if printer does not advertise mono mode or
      // sRGB if DeviceRGB is requested but only sRGB available)
      // AdobeRGB instead of sRGB only if available in 16 bit per color and
      // high color depth is requested
      if ((cspace_fallback == 1 &&
	   (!strcasecmp(color_mode, "auto") ||
	    strcasestr(color_mode, "color") ||
	    strcasestr(color_mode, "rgb") ||
	    strcasestr(color_mode, "cmy"))) ||
	  cspace_fallback == 2)
      {
	if (strcasestr(color_mode, "adobe") ||
	    (high_depth && *high_depth &&
	     (strstr(available, "ADOBERGB24-48") ||
	      strstr(available, "ADOBERGB48") ||
	      strstr(available, "adobe-rgb_16"))))
	  *cspace = CUPS_CSPACE_ADOBERGB;
	else if (strcasestr(available, "cmyk") &&
		 strcasestr(color_mode, "cmy"))
	  *cspace = CUPS_CSPACE_CMYK;
	else if ((strcasestr(available, "srgb") &&
		  !strcasestr(color_mode, "device")) ||
		 cspace_fallback == 2)
	  *cspace = CUPS_CSPACE_SRGB;
	else
	  *cspace = CUPS_CSPACE_RGB;
      }
      else
      {
	if (!strcasestr(color_mode, "device"))
	  *cspace = CUPS_CSPACE_SW;
	else
	  *cspace = CUPS_CSPACE_W;
      }
    }
  }

  // Success, update the raster header
  h->cupsBitsPerColor = best_depth;
  h->cupsBitsPerPixel = best_depth * num_colors;
  h->cupsColorSpace = *cspace;
  h->cupsNumColors = num_colors;
  h->cupsBytesPerLine = (h->cupsWidth * h->cupsBitsPerPixel + 7) / 8;

  return (0);
}


static int                                 // O - -1 on error, 0 on success
raster_base_header(cups_page_header2_t *h, // O - Raster header
		   cf_filter_data_t *data, // I - Filter data
		   int pwg_raster)         // I - 1 if PWG/Apple Raster
{
  int		i;			// Looping var
  char		*ptr,			// Pointer into string
		s[255];			// Temporary string
  const char	*val,			// Pointer into value
                *media;			// media option
  char		*media_source,          // Media source
                *media_type;		// Media type
  pwg_media_t   *size_found;            // page size found for given name
  int           num_options = 0;        // Number of options
  cups_option_t *options = NULL;        // Options
  ipp_attribute_t *attr;


  //
  // Range check input...
  //

  if (!h)
    return (-1);

  //
  // Join the IPP attributes and the CUPS options in a single list
  //

  num_options = cfJoinJobOptionsAndAttrs(data, num_options, &options);

  //
  // Check if the supplied "media" option is a comma-separated list of any
  // combination of page size ("media"), media source ("media-position"),
  // and media type ("media-type") and if so, extract media type and source.
  // Media size will be handled separately.
  //

  media_source = NULL;
  media_type = NULL;
  if ((media = cupsGetOption("media", num_options, options)) != NULL)
  {
    //
    // Loop through the option string, separating it at commas and setting each
    // individual option.
    //
    // For PageSize, we also check for an empty option value since some versions
    // of MacOS X use it to specify auto-selection of the media based solely on
    // the size.
    //

    for (val = media; *val;)
    {
      //
      // Extract the sub-option from the string...
      //

      for (ptr = s; *val && *val != ',' && (ptr - s) < (sizeof(s) - 1);)
	*ptr++ = *val++;
      *ptr++ = '\0';

      if (*val == ',')
	val ++;

      //
      // Identify it...
      //

      size_found = NULL;
      if ((size_found = pwgMediaForPWG(s)) == NULL)
	if ((size_found = pwgMediaForPPD(s)) == NULL)
	  if ((size_found = pwgMediaForLegacy(s)) == NULL)
	  {
	    if (strcasestr(s, "tray") ||
		strcasestr(s, "feed") ||
		strcasestr(s, "capacity") ||
		strcasestr(s, "upper") ||
		strcasestr(s, "top") ||
		strcasestr(s, "middle") ||
		strcasestr(s, "lower") ||
		strcasestr(s, "bottom") ||
		strcasestr(s, "left") ||
		strcasestr(s, "right") ||
		strcasestr(s, "side") ||
		strcasestr(s, "roll") ||
		strcasestr(s, "main"))
            { 
              if (media_source == NULL)
	        media_source = strdup(s);
            }
	    else
	      media_type = strdup(s);
	  }
    }
  }

  //
  // Initialize header
  //

  memset(h, 0, sizeof(cups_page_header2_t));

  //
  // Fill in the items using printer and job IPP attributes and options
  //

  if (pwg_raster)
    strcpy(h->MediaClass, "PwgRaster");
  else if ((val = cupsGetOption("media-class", num_options, options)) != NULL ||
	   (val = cupsGetOption("MediaClass", num_options, options)) != NULL)
    _strlcpy(h->MediaClass, val, sizeof(h->MediaClass));
  else
    strcpy(h->MediaClass, "");
  if (strcasecmp(h->MediaClass, "PwgRaster") == 0)
    pwg_raster = 1;

  if ((val = cupsGetOption("media-color", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaColor", num_options, options)) != NULL)
    _strlcpy(h->MediaColor, val, sizeof(h->MediaColor));
  else
    h->MediaColor[0] = '\0';

  if ((val = cupsGetOption("media-type", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaType", num_options, options)) != NULL ||
      (val = media_type) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					data->job_attrs,
					"media-type")) != NULL)
    _strlcpy(h->MediaType, val, sizeof(h->MediaType));
  else
    h->MediaType[0] = '\0';

  if ((val = cupsGetOption("print-content-optimize", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("output-type", num_options, options)) != NULL ||
      (val = cupsGetOption("OutputType", num_options, options)) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					 data->job_attrs,
					"print-content-optimize")) != NULL)
  {
    if (!strncasecmp(val, "auto", 4))
      _strlcpy(h->OutputType, "automatic",
	      sizeof(h->OutputType));
    else if (!strcasecmp(val, "graphics") ||
	     !strcasecmp(val, "graphic"))
      _strlcpy(h->OutputType, "graphics", sizeof(h->OutputType));
    else if (!strcasecmp(val, "photo"))
      _strlcpy(h->OutputType, "photo", sizeof(h->OutputType));
    else if (!strcasecmp(val, "text"))
      _strlcpy(h->OutputType, "text", sizeof(h->OutputType));
    else if (!strcasecmp(val, "text-and-graphics") ||
	     !strcasecmp(val, "text-and-graphic") ||
	     !strcasecmp(val, "TextAndGraphics") ||
	     !strcasecmp(val, "TextAndGraphic"))
      _strlcpy(h->OutputType, "text-and-graphics",
	      sizeof(h->OutputType));
    else if (!pwg_raster)
      _strlcpy(h->OutputType, val, sizeof(h->OutputType));
  }
  else
    _strlcpy(h->OutputType, "automatic", sizeof(h->OutputType));

  if (pwg_raster)
  {
    // Set "reserved" fields to 0
    h->AdvanceDistance = 0;
    h->AdvanceMedia = CUPS_ADVANCE_NONE;
    h->Collate = CUPS_FALSE;
  }
  else
  {
    // TODO - Support for advance distance and advance media
    h->AdvanceDistance = 0;
    h->AdvanceMedia = CUPS_ADVANCE_NONE;
    if ((val = cupsGetOption("Collate", num_options, options)) != NULL ||
	(val = cupsGetOption("multiple-document-handling",
			     num_options, options)) != NULL ||
	(val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					  data->job_attrs,
					  "multiple-document-handling")) !=
	 NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes") ||
	  !strcasecmp(val, "separate-documents-collated-copies"))
	h->Collate = CUPS_TRUE;
      else
	h->Collate = CUPS_FALSE;
    }
    else
      h->Collate = CUPS_FALSE;
  }

  h->CutMedia = CUPS_CUT_NONE;

  h->Tumble = CUPS_FALSE;
  if ((val = cupsGetOption("sides", num_options, options)) != NULL ||
      (val = cupsGetOption("Duplex", num_options, options)) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					data->job_attrs,
					"sides")) != NULL)
  {
    if (!strcasecmp(val, "None") || !strcasecmp(val, "Off") ||
	!strcasecmp(val, "False") || !strcasecmp(val, "No") ||
	!strcasecmp(val, "one-sided") || !strcasecmp(val, "OneSided"))
      h->Duplex = CUPS_FALSE;
    else if (!strcasecmp(val, "On") ||
	     !strcasecmp(val, "True") || !strcasecmp(val, "Yes") ||
	     !strncasecmp(val, "two-sided", 9) ||
	     !strncasecmp(val, "TwoSided", 8) ||
	     !strncasecmp(val, "Duplex", 6))
    {
      h->Duplex = CUPS_TRUE;
      if (!strncasecmp(val, "DuplexTumble", 12) ||
	  strcasestr(val, "short-edge"))
	h->Tumble = CUPS_TRUE;
      if (!strncasecmp(val, "DuplexNoTumble", 14) ||
	  strcasestr(val, "long-edge"))
	h->Tumble = CUPS_FALSE;
    }
    else
      h->Duplex = CUPS_FALSE;
  }
  else
    h->Duplex = CUPS_FALSE;

  if ((val = cupsGetOption("printer-resolution", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("Resolution", num_options, options)) != NULL)
  {
    int	        xres,		// X resolution
                yres;		// Y resolution
    char	*ptr;		// Pointer into value

    xres = yres = strtol(val, (char **)&ptr, 10);
    if (ptr > val && xres > 0)
    {
      if (*ptr == 'x')
	yres = strtol(ptr + 1, (char **)&ptr, 10);
    }

    if (ptr > val && xres > 0 && yres > 0 && ptr &&
	(*ptr == '\0' ||
	 !strcasecmp(ptr, "dpi") ||
	 !strcasecmp(ptr, "dpc") ||
	 !strcasecmp(ptr, "dpcm")))
    {
      if (!strcasecmp(ptr, "dpc") ||
	  !strcasecmp(ptr, "dpcm"))
      {
	xres = xres * 254 / 100;
	yres = yres * 254 / 100;
      }
      h->HWResolution[0] = xres;
      h->HWResolution[1] = yres;
    }
    else
    {
      h->HWResolution[0] = 100; // Resolution invalid
      h->HWResolution[1] = 100;
    }
  }
  else
  {
    h->HWResolution[0] = 100; // Resolution invalid
    h->HWResolution[1] = 100;
  }

  // Resolution from IPP attrs
  if (h->HWResolution[0] == 100 && h->HWResolution[1] == 100)
  {
    int x = 0, y = 0;
    cfIPPAttrResolutionForPrinter(data->printer_attrs, data->job_attrs,
				  NULL, &x, &y);
    if (x && y)
    {
      h->HWResolution[0] = x;
      h->HWResolution[1] = y;
    }
  }
  
  // TODO - Support for insert sheets
  h->InsertSheet = CUPS_FALSE;

  // TODO - Support for jog
  h->Jog = CUPS_JOG_NONE;

  if ((val = cupsGetOption("feed-orientation", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("feed-direction", num_options, options)) != NULL ||
      (val = cupsGetOption("LeadingEdge", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "ShortEdgeFirst"))
      h->LeadingEdge = CUPS_EDGE_TOP;
    else if (!strcasecmp(val, "LongEdgeFirst"))
      h->LeadingEdge = CUPS_EDGE_RIGHT;
  }
  else
    h->LeadingEdge = CUPS_EDGE_TOP;

  // TODO - Support for manual feed
  h->ManualFeed = CUPS_FALSE;

  if ((val = cupsGetOption("media-position", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaPosition", num_options, options)) != NULL ||
      (val = cupsGetOption("media-source", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaSource", num_options, options)) != NULL ||
      (val = cupsGetOption("InputSlot", num_options, options)) != NULL ||
      (val = media_source) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					data->job_attrs,
					"media-source")) != NULL)
  {
    if (!strncasecmp(val, "Auto", 4) ||
	!strncasecmp(val, "Default", 7))
      h->MediaPosition = 0;
    else if (!strcasecmp(val, "Main"))
      h->MediaPosition = 1;
    else if (!strcasecmp(val, "Alternate"))
      h->MediaPosition = 2;
    else if (!strcasecmp(val, "LargeCapacity") ||
	     !strcasecmp(val, "large-capacity"))
      h->MediaPosition = 3;
    else if (!strcasecmp(val, "Manual"))
      h->MediaPosition = 4;
    else if (!strcasecmp(val, "Envelope"))
      h->MediaPosition = 5;
    else if (!strcasecmp(val, "Disc"))
      h->MediaPosition = 6;
    else if (!strcasecmp(val, "Photo"))
      h->MediaPosition = 7;
    else if (!strcasecmp(val, "Hagaki"))
      h->MediaPosition = 8;
    else if (!strcasecmp(val, "MainRoll") ||
	     !strcasecmp(val, "main-roll"))
      h->MediaPosition = 9;
    else if (!strcasecmp(val, "AlternateRoll") ||
	     !strcasecmp(val, "alternate-roll"))
      h->MediaPosition = 10;
    else if (!strcasecmp(val, "Top"))
      h->MediaPosition = 11;
    else if (!strcasecmp(val, "Middle"))
      h->MediaPosition = 12;
    else if (!strcasecmp(val, "Bottom"))
      h->MediaPosition = 13;
    else if (!strcasecmp(val, "Side"))
      h->MediaPosition = 14;
    else if (!strcasecmp(val, "Left"))
      h->MediaPosition = 15;
    else if (!strcasecmp(val, "Right"))
      h->MediaPosition = 16;
    else if (!strcasecmp(val, "Center"))
      h->MediaPosition = 17;
    else if (!strcasecmp(val, "Rear"))
      h->MediaPosition = 18;
    else if (!strcasecmp(val, "ByPassTray") ||
	     !strcasecmp(val, "bypass-tray"))
      h->MediaPosition = 19;
    else if (!strcasecmp(val, "Tray1") ||
	     !strcasecmp(val, "tray-1"))
      h->MediaPosition = 20;
    else if (!strcasecmp(val, "Tray2") ||
	     !strcasecmp(val, "tray-2"))
      h->MediaPosition = 21;
    else if (!strcasecmp(val, "Tray3") ||
	     !strcasecmp(val, "tray-3"))
      h->MediaPosition = 22;
    else if (!strcasecmp(val, "Tray4") ||
	     !strcasecmp(val, "tray-4"))
      h->MediaPosition = 23;
    else if (!strcasecmp(val, "Tray5") ||
	     !strcasecmp(val, "tray-5"))
      h->MediaPosition = 24;
    else if (!strcasecmp(val, "Tray6") ||
	     !strcasecmp(val, "tray-6"))
      h->MediaPosition = 25;
    else if (!strcasecmp(val, "Tray7") ||
	     !strcasecmp(val, "tray-7"))
      h->MediaPosition = 26;
    else if (!strcasecmp(val, "Tray8") ||
	     !strcasecmp(val, "tray-8"))
      h->MediaPosition = 27;
    else if (!strcasecmp(val, "Tray9") ||
	     !strcasecmp(val, "tray-9"))
      h->MediaPosition = 28;
    else if (!strcasecmp(val, "Tray10") ||
	     !strcasecmp(val, "tray-10"))
      h->MediaPosition = 29;
    else if (!strcasecmp(val, "Tray11") ||
	     !strcasecmp(val, "tray-11"))
      h->MediaPosition = 30;
    else if (!strcasecmp(val, "Tray12") ||
	     !strcasecmp(val, "tray-12"))
      h->MediaPosition = 31;
    else if (!strcasecmp(val, "Tray13") ||
	     !strcasecmp(val, "tray-13"))
      h->MediaPosition = 32;
    else if (!strcasecmp(val, "Tray14") ||
	     !strcasecmp(val, "tray-14"))
      h->MediaPosition = 33;
    else if (!strcasecmp(val, "Tray15") ||
	     !strcasecmp(val, "tray-15"))
      h->MediaPosition = 34;
    else if (!strcasecmp(val, "Tray16") ||
	     !strcasecmp(val, "tray-16"))
      h->MediaPosition = 35;
    else if (!strcasecmp(val, "Tray17") ||
	     !strcasecmp(val, "tray-17"))
      h->MediaPosition = 36;
    else if (!strcasecmp(val, "Tray18") ||
	     !strcasecmp(val, "tray-18"))
      h->MediaPosition = 37;
    else if (!strcasecmp(val, "Tray19") ||
	     !strcasecmp(val, "tray-19"))
      h->MediaPosition = 38;
    else if (!strcasecmp(val, "Tray20") ||
	     !strcasecmp(val, "tray-20"))
      h->MediaPosition = 39;
    else if (!strcasecmp(val, "Roll1") ||
	     !strcasecmp(val, "roll-1"))
      h->MediaPosition = 40;
    else if (!strcasecmp(val, "Roll2") ||
	     !strcasecmp(val, "roll-2"))
      h->MediaPosition = 41;
    else if (!strcasecmp(val, "Roll3") ||
	     !strcasecmp(val, "roll-3"))
      h->MediaPosition = 42;
    else if (!strcasecmp(val, "Roll4") ||
	     !strcasecmp(val, "roll-4"))
      h->MediaPosition = 43;
    else if (!strcasecmp(val, "Roll5") ||
	     !strcasecmp(val, "roll-5"))
      h->MediaPosition = 44;
    else if (!strcasecmp(val, "Roll6") ||
	     !strcasecmp(val, "roll-6"))
      h->MediaPosition = 45;
    else if (!strcasecmp(val, "Roll7") ||
	     !strcasecmp(val, "roll-7"))
      h->MediaPosition = 46;
    else if (!strcasecmp(val, "Roll8") ||
	     !strcasecmp(val, "roll-8"))
      h->MediaPosition = 47;
    else if (!strcasecmp(val, "Roll9") ||
	     !strcasecmp(val, "roll-9"))
      h->MediaPosition = 48;
    else if (!strcasecmp(val, "Roll10") ||
	     !strcasecmp(val, "roll-10"))
      h->MediaPosition = 49;
  }
  else
    h->MediaPosition = 0; // Auto

  if ((val = cupsGetOption("media-weight", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaWeight", num_options, options)) != NULL ||
      (val = cupsGetOption("media-weight-metric", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("MediaWeightMetric", num_options, options)) != NULL)
    h->MediaWeight = atol(val);
  else
    h->MediaWeight = 0;

  if (pwg_raster)
  {
    // Set "reserved" fields to 0
    h->MirrorPrint = CUPS_FALSE;
    h->NegativePrint = CUPS_FALSE;
  }
  else
  {
    if ((val = cupsGetOption("mirror-print", num_options, options)) != NULL ||
	(val = cupsGetOption("MirrorPrint", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes"))
	h->MirrorPrint = CUPS_TRUE;
      else if (!strcasecmp(val, "false") ||
	       !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no"))
	h->MirrorPrint = CUPS_FALSE;
      else
	h->MirrorPrint = CUPS_FALSE;
    }
    if ((val = cupsGetOption("negative-print", num_options, options)) != NULL ||
	(val = cupsGetOption("NegativePrint", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes"))
	h->NegativePrint = CUPS_TRUE;
      else if (!strcasecmp(val, "false") ||
	       !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no"))
	h->NegativePrint = CUPS_FALSE;
      else
	h->NegativePrint = CUPS_FALSE;
    }
  }

  i = 0;
  if ((val = cupsGetOption("copies", num_options, options)) != NULL ||
      (val = cupsGetOption("Copies", num_options, options)) != NULL ||
      (val = cupsGetOption("num-copies", num_options, options)) != NULL ||
      (val = cupsGetOption("NumCopies", num_options, options)) != NULL ||
      cfIPPAttrIntValForPrinter(data->printer_attrs, data->job_attrs,
				"copies", &i) == 1)
  {
    if (val)
      h->NumCopies = atol(val);
    else if (i)
      h->NumCopies = i;
  }
  else
    h->NumCopies = 1; // 0 = Printer default

  if ((val = cupsGetOption("orientation-requested", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("OrientationRequested", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("Orientation", num_options, options)) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					data->job_attrs,
					"orientation-requested")) != NULL)
  {
    if (!strcasecmp(val, "Portrait") ||
	!strcasecmp(val, "3") ||
	!strcasecmp(val, "0"))
      h->Orientation = CUPS_ORIENT_0;
    else if (!strcasecmp(val, "Landscape") ||
	     !strcasecmp(val, "4"))
      h->Orientation = CUPS_ORIENT_90;
    else if (!strcasecmp(val, "reverse-portrait") ||
	     !strcasecmp(val, "ReversePortrait") ||
	     !strcasecmp(val, "5"))
      h->Orientation = CUPS_ORIENT_180;
    else if (!strcasecmp(val, "reverse-landscape") ||
	     !strcasecmp(val, "ReverseLandscape") ||
	     !strcasecmp(val, "6"))
      h->Orientation = CUPS_ORIENT_270;
    else
      h->Orientation = CUPS_ORIENT_0;
  }
  else
    h->Orientation = CUPS_ORIENT_0;

  if (pwg_raster)
  {
    // Set "reserved" fields to 0
    h->OutputFaceUp = CUPS_FALSE;
  }
  else
  {
    if ((val = cupsGetOption("OutputFaceUp", num_options, options)) != NULL ||
	(val = cupsGetOption("output-face-up", num_options, options)) != NULL ||
	(val = cupsGetOption("OutputBin", num_options, options)) != NULL ||
	(val = cupsGetOption("output-bin", num_options, options)) != NULL ||
	(val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					  data->job_attrs,
					  "output-bin")) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes") || !strcasecmp(val, "face-up") ||
	  !strcasecmp(val, "FaceUp"))
	h->OutputFaceUp = CUPS_TRUE;
      else if (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no") || !strcasecmp(val, "face-down") ||
	       !strcasecmp(val, "FaceDown"))
	h->OutputFaceUp = CUPS_FALSE;
      else
	h->OutputFaceUp = CUPS_FALSE;
    }
  }

  if (pwg_raster)
  {
    // Set "reserved" fields to 0
    h->Separations = CUPS_FALSE;
    h->TraySwitch = CUPS_FALSE;
  }
  else
  {
    if ((val = cupsGetOption("separations", num_options, options)) != NULL ||
	(val = cupsGetOption("Separations", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes"))
	h->Separations = CUPS_TRUE;
      else if (!strcasecmp(val, "false") ||
	       !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no"))
	h->Separations = CUPS_FALSE;
      else
	h->Separations = CUPS_FALSE;
    }
    if ((val = cupsGetOption("tray-switch", num_options, options)) != NULL ||
	(val = cupsGetOption("TraySwitch", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes"))
	h->TraySwitch = CUPS_TRUE;
      else if (!strcasecmp(val, "false") ||
	       !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no"))
	h->TraySwitch = CUPS_FALSE;
      else
	h->TraySwitch = CUPS_FALSE;
    }
  }

  if ((val = cupsGetOption("Tumble", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "Off") || !strcasecmp(val, "False") ||
	!strcasecmp(val, "No"))
      h->Tumble = CUPS_FALSE;
    else if (!strcasecmp(val, "On") || !strcasecmp(val, "True") ||
	     !strcasecmp(val, "Yes"))
      h->Tumble = CUPS_TRUE;
  }

  // TODO - Support for MediaType number
  h->cupsMediaType = 0;

  // Only for CUPS Raster, if we do not have a sample header from a PPD file
  if (pwg_raster == 0 &&
      ((val = cupsGetOption("pwg-raster-document-type", num_options,
			    options)) != NULL ||
       (val = cupsGetOption("PwgRasterDocumentType", num_options,
			    options)) != NULL ||
       (val = cupsGetOption("color-space", num_options, options)) != NULL ||
       (val = cupsGetOption("ColorSpace", num_options, options)) != NULL ||
       (val = cupsGetOption("color-model", num_options, options)) != NULL ||
       (val = cupsGetOption("ColorModel", num_options, options)) != NULL ||
       (val = cupsGetOption("print-color-mode", num_options, options)) !=
       NULL ||
       (val = cupsGetOption("output-mode", num_options, options)) != NULL ||
       (val = cupsGetOption("OutputMode", num_options, options)) != NULL ||
       (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					 data->job_attrs,
					 "print-color-mode")) != NULL))
  {
    int	        bitspercolor,	// Bits per color
                bitsperpixel,   // Bits per pixel
                colorspace,     // CUPS/PWG raster color space
                numcolors;	// Number of colorants
    const char	*ptr;		// Pointer into value

    ptr = NULL;
    numcolors = 0;
    bitspercolor = 8;
    if (!strncasecmp(val, "AdobeRgb", 8))
    {
      if (*(val + 8) == '_' || *(val + 8) == '-')
	ptr = val + 9;
      colorspace = 20;
      numcolors = 3;
    }
    else if (!strncasecmp(val, "adobe-rgb", 9))
    {
      if (*(val + 9) == '_' || *(val + 9) == '-')
	ptr = val + 10;
      colorspace = 20;
      numcolors = 3;
    }
    else if (!strcasecmp(val, "auto-monochrome"))
    {
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strcasecmp(val, "bi-level") ||
	     !strcasecmp(val, "process-bi-level"))
    {
      bitspercolor = 1;
      colorspace = 3;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Black", 5))
    {
      if (*(val + 5) == '_' || *(val + 5) == '-')
	ptr = val + 6;
      bitspercolor = 1;
      colorspace = 3;
      numcolors = 1;
    }
    else if (!strcasecmp(val, "process-monochrome"))
    {
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Monochrome", 10))
    {
      if (*(val + 10) == '_' || *(val + 10) == '-')
	ptr = val + 11;
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Mono", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-')
	ptr = val + 5;
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strcasecmp(val, "color"))
    {
      colorspace = 19;
      numcolors = 3;
    }
    else if (!strncasecmp(val, "Cmyk", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-')
	ptr = val + 5;
      colorspace = 6;
      numcolors = 4;
    }
    else if (!strncasecmp(val, "Cmy", 3))
    {
      if (*(val + 3) == '_' || *(val + 3) == '-')
	ptr = val + 4;
      colorspace = 4;
      numcolors = 3;
    }
    else if (!strncasecmp(val, "Device", 6))
    {
      ptr = val + 6;
      numcolors = strtol(ptr, (char **)&ptr, 10);
      if (*ptr == '_' || *ptr == '-')
      {
	ptr ++;
	colorspace = 47 + numcolors;
      }
      else
      {
	numcolors = 0;
	ptr = NULL;
      }
    }
    else if (!strncasecmp(val, "Sgray", 5))
    {
      if (*(val + 5) == '_' || *(val + 5) == '-')
	ptr = val + 6;
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Gray", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-')
	ptr = val + 5;
      colorspace = 18;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Srgb", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-')
	ptr = val + 5;
      colorspace = 19;
      numcolors = 3;
    }
    else if (!strncasecmp(val, "Rgbw", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-')
	ptr = val + 5;
      colorspace = 17;
      numcolors = 4;
    }
    else if (!strncasecmp(val, "Rgb", 3))
    {
      if (*(val + 3) == '_' || *(val + 3) == '-')
	ptr = val + 4;
      colorspace = 1;
      numcolors = 3;
    }
    else if (!strcasecmp(val, "auto"))
    {
      // Let "auto" not look like an error
      colorspace = 19;
      numcolors = 3;
    }
    if (numcolors > 0)
    {
      if (ptr)
	bitspercolor = strtol(ptr, (char **)&ptr, 10);
      bitsperpixel = bitspercolor * numcolors;
      // In 1-bit-per-color RGB modes we add a forth bit to each pixel
      // to align the pixels with bytes
      if (bitsperpixel == 3 &&
	  strcasestr(val, "Rgb"))
	bitsperpixel = 4;
      h->cupsBitsPerColor = bitspercolor;
      h->cupsBitsPerPixel = bitsperpixel;
      h->cupsColorSpace = colorspace;
      h->cupsNumColors = numcolors;
    }
    else
    {
      h->cupsBitsPerColor = 8;
      h->cupsBitsPerPixel = 24;
      h->cupsColorSpace = 19;
      h->cupsNumColors = 3;
    }
  }
  else
  {
    h->cupsBitsPerColor = 8;
    h->cupsBitsPerPixel = 24;
    h->cupsColorSpace = 19;
    h->cupsNumColors = 3;
  }

  // TODO - Support for color orders 1 (banded) and 2 (planar)
  h->cupsColorOrder = 0;
  
  // TODO - Support for these parameters
  h->cupsCompression = 0;
  h->cupsRowCount = 0;
  h->cupsRowFeed = 0;
  h->cupsRowStep = 0;

  // TODO - Support for cupsBorderlessScalingFactor
  h->cupsBorderlessScalingFactor = 0.0;

  // TODO - Support for custom values in CUPS Raster mode
  for (i = 0; i < 16; i ++)
  {
    h->cupsInteger[i] = 0;
    h->cupsReal[i] = 0.0;
    memset(h->cupsString[i], 0, 64);
  }

  if (pwg_raster)
  {
    
    if ((val = cupsGetOption("job-impressions", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("JobImpressions", num_options, options)) != NULL ||
	(val = cupsGetOption("Impressions", num_options, options)) != NULL)
    {
      int impressions = atoi(val);
      if (impressions >= 0)
	h->cupsInteger[0] = impressions;
    }

    // Printer property, command line options only for development and
    // debugging
    if ((val = cupsGetOption("pwg-raster-document-sheet-back", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("back-side-orientation", num_options,
			     options)) != NULL ||
	(data->printer_attrs &&
	 ((attr = ippFindAttribute(data->printer_attrs, "urf-supported",
				   IPP_TAG_ZERO)) != NULL ||
	  (attr = ippFindAttribute(data->printer_attrs,
				   "pwg-raster-document-sheet-back",
				   IPP_TAG_ZERO)) != NULL ||
	  (attr = ippFindAttribute(data->printer_attrs,
				   "pclm-raster-back-side",
				   IPP_TAG_ZERO)) != NULL)))
    {
      if (val == NULL)
      {
	if (ippGetCount(attr) > 1) // urf-supported
	{
	  for (i = 0; i < ippGetCount(attr); i ++)
	  {
	    val = ippGetString(attr, i, NULL);
	    if (strncmp(val, "DM", 2) == 0) // Duplex mode field
	      break;
	  }
	  if (i == ippGetCount(attr)) // Not found, no duplex
	    val = "DM1";
	}
	else // pwg-raster-document-sheet-back/pclm-raster-back-side
	  val = ippGetString(attr, 0, NULL);
      }
      // Set CrossFeedTransform and FeedTransform
      if (h->Duplex == CUPS_FALSE)
      {
	h->cupsInteger[1] = 1;
	h->cupsInteger[2] = 1;
      }
      else if (h->Duplex == CUPS_TRUE)
      {
	if (h->Tumble == CUPS_FALSE)
	{
	  if (!strcasecmp(val, "Flipped") ||
	      !strcasecmp(val, "DM2"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] = -1;
	  }
	  else if (!strncasecmp(val, "Manual", 6) ||
		   !strcasecmp(val, "DM4"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Normal") ||
		   !strcasecmp(val, "DM1"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Rotated") ||
		   !strcasecmp(val, "DM3"))
	  {
	    h->cupsInteger[1] = -1;
	    h->cupsInteger[2] = -1;
	  }
	  else 
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	}
	else
	{
	  if (!strcasecmp(val, "Flipped") ||
	      !strcasecmp(val, "DM2"))
	  {
	    h->cupsInteger[1] = -1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strncasecmp(val, "Manual", 6) ||
		   !strcasecmp(val, "DM4"))
	  {
	    h->cupsInteger[1] = -1;
	    h->cupsInteger[2] = -1;
	  }
	  else if (!strcasecmp(val, "Normal") ||
		   !strcasecmp(val, "DM1"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Rotated") ||
		   !strcasecmp(val, "DM3"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else 
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	}
      }
      else
      {
	h->cupsInteger[1] = 1;
	h->cupsInteger[2] = 1;
      }
    }
    else
    {
      h->cupsInteger[1] = 1;
      h->cupsInteger[2] = 1;
    }

    // TODO - Support for ImageBoxLeft, ImageBoxTop, ImageBoxRight, and
    // ImageBoxBottom (h->cupsInteger[3..6]), leave on 0 for now

    if ((val = cupsGetOption("alternate-primary", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("AlternatePrimary", num_options,
			     options)) != NULL)
    {
      int alternateprimary = atoi(val);		// SRGB value for black
						// pixels
      h->cupsInteger[7] = alternateprimary;
    }

    if ((val = cupsGetOption("print-quality", num_options, options)) != NULL ||
	(val = cupsGetOption("PrintQuality", num_options, options)) != NULL ||
	(val = cupsGetOption("Quality", num_options, options)) != NULL)
    {
      int quality = atoi(val);		// print-quality value

      if (!quality ||
	  (quality >= IPP_QUALITY_DRAFT && quality <= IPP_QUALITY_HIGH))
	h->cupsInteger[8] = quality;
    }

    // Leave "reserved" fields (h->cupsInteger[9..13]) on 0

    if ((val = cupsGetOption("vendor-identifier", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("VendorIdentifier", num_options,
			     options)) != NULL)
    {
      int vendorid = atoi(val);		// USB ID of manufacturer
      h->cupsInteger[14] = vendorid;
    }

    if ((val = cupsGetOption("vendor-length", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("VendorLength", num_options,
			     options)) != NULL)
    {
      int vendorlength = atoi(val);		// How many bytes of vendor
						// data?
      if (vendorlength > 0 && vendorlength <= 1088)
      {
	h->cupsInteger[15] = vendorlength;
	if ((val = cupsGetOption("vendor-data", num_options,
				 options)) != NULL ||
	    (val = cupsGetOption("VendorData", num_options,
				 options)) != NULL)
	  // TODO - How to enter binary data here?
	  _strlcpy((char *)&(h->cupsReal[0]), val, 1088);
      }
    }
  }

  // Set "reserved" fields to 0
  memset(h->cupsMarkerType, 0, 64);

  if ((val = cupsGetOption("print-rendering-intent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PrintRenderingIntent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("RenderingIntent", num_options,
			   options)) != NULL ||
      (val = cfIPPAttrEnumValForPrinter(data->printer_attrs,
					data->job_attrs,
					"print-rendering-intent")) != NULL)
  {
    if (!strcmp(val, "absolute"))
      _strlcpy(h->cupsRenderingIntent, "Absolute",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "automatic"))
      _strlcpy(h->cupsRenderingIntent, "Automatic",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "perceptual"))
      _strlcpy(h->cupsRenderingIntent, "Perceptual",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "relative"))
      _strlcpy(h->cupsRenderingIntent, "Relative",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "relative-bpc") ||
	     !strcmp(val, "RelativeBpc"))
      _strlcpy(h->cupsRenderingIntent, "RelativeBpc",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "saturation"))
      _strlcpy(h->cupsRenderingIntent, "Saturation",
	      sizeof(h->cupsRenderingIntent));
  }
  else
    h->cupsRenderingIntent[0] = '\0';

  if (media_source != NULL)
    free(media_source);
  if (media_type != NULL)
    free(media_type);
  cupsFreeOptions(num_options, options);

  return (0);
}
