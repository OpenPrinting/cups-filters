/*
 *   Function to apply IPP options to a CUPS/PWG Raster header.
 *
 *   Copyright 2013 by Till Kamppeter.
 *
 *   Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file.
 *
 * Contents:
 *
 *   cupsRasterParseIPPOptions() - Parse IPP options from the command line
 *                                 and apply them to the CUPS Raster header.
 */

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 6)
#define HAVE_CUPS_1_7 1
#endif

/*
 * Include necessary headers.
 */

#include "raster.h"
#include "filter.h"
#include "driver.h"
#include "ipp.h"
#include <string.h>
#include <ctype.h>
#ifdef HAVE_CUPS_1_7
#include <cups/pwg.h>
#endif /* HAVE_CUPS_1_7 */

/*
 * '_strlcpy()' - Safely copy two strings.
 */

size_t					/* O - Length of string */
_strlcpy(char       *dst,		/* O - Destination string */
	 const char *src,		/* I - Source string */
	 size_t      size)		/* I - Size of destination string buffer */
{
  size_t	srclen;			/* Length of source string */


 /*
  * Figure out how much room is needed...
  */

  size --;

  srclen = strlen(src);

 /*
  * Copy the appropriate amount...
  */

  if (srclen > size)
    srclen = size;

  memcpy(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}


/*
 * 'cupsRasterPrepareHeader() - This function creates a CUPS/PWG
 *                              Raster header for Raster output based
 *                              on the printer and job properties
 *                              supplied to the calling filter
 *                              functions, printer properties via PPD
 *                              file or printer IPP attributes and job
 *                              properties via CUPS option list and
 *                              job IPP attributes. For PWG and Apple
 *                              Raster output the color space and
 *                              depth is auto-selected based on
 *                              available options listed in the
 *                              urf-supported and
 *                              pwg-raster-document-type-supported
 *                              printer IPP attributes (PPD generator
 *                              adds those also to the PPD) and the
 *                              settings of print-color-mode
 *                              ("ColorModel") and print-quality
 *                              ("cupsPrintQuality") job
 *                              attributes/options.
 */

int                                             /* O  - 0 on success,
						        -1 on error */
cupsRasterPrepareHeader(cups_page_header2_t *h, /* I  - Raster header */
			filter_data_t *data,    /* I  - Job and printer data */
			filter_out_format_t final_content_type,
                                                /* I  - Job output format */
			cups_cspace_t *cspace)  /* IO - Color space we want to
						        use, -1 for auto, we
							return color space
							actually used, -1 if
						        no suitable color space
						        found. */
{
  ppd_file_t *ppd;
  ipp_t *printer_attrs, *job_attrs;
  int num_options = 0;
  cups_option_t *options = NULL;
  filter_logfunc_t log = data->logfunc;
  void          *ld = data->logdata;
  int pwgraster = 0,
    appleraster = 0,
    cupsraster = 0;
  const char *p;
  ppd_attr_t *ppd_attr;
  ipp_attribute_t *attr;
  const char *cspaces_available, *color_mode, *quality;
  int hi_depth;
  ppd_choice_t *choice;
  char valuebuffer[2048];
  int res = 1;
  int xres = -1; int yres = -1;
  
  printer_attrs = data->printer_attrs;
  job_attrs = data->job_attrs;

  num_options = joinJobOptionsAndAttrs(data, num_options, &options);
  ppd = data->ppd;
  printer_attrs = data->printer_attrs;
  job_attrs = data->job_attrs;

  if (final_content_type == OUTPUT_FORMAT_PWG_RASTER)
    pwgraster = 1;
  else if (final_content_type == OUTPUT_FORMAT_APPLE_RASTER)
    appleraster = 1;
  else
    cupsraster = 1;
    /*  These values will be used in case we don't find supported resolutions
        for given OUTFORMAT */
  if((attr = ippFindAttribute(printer_attrs, "printer-resolution-default", IPP_TAG_RESOLUTION))!=NULL)
  {
    ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
    const char *p = valuebuffer;
    xres = atoi(p);
    if((p = strchr(p, 'x'))!=NULL) yres = atoi(p+1);
    else yres = xres;
  }
  /*  Finding supported resolution for given outFormat  */
  if(pwgraster){
    if((attr = ippFindAttribute(printer_attrs, "pwg-raster-document-resolution-supported",
     IPP_TAG_RESOLUTION))!=NULL)
    {
      strncpy(valuebuffer, ippGetString(attr, 0, NULL), 
        sizeof(valuebuffer)-1);
      const char *p = valuebuffer;
      xres = atoi(p);
      if((p = strchr(p, 'x'))!=NULL)
        yres = atoi(p+1);
      else 
        yres = xres;
    }
  }
  else if(appleraster){
    if((attr = ippFindAttribute(printer_attrs, "urf-supported",
     IPP_TAG_KEYWORD))!=NULL)
    {
      for(int i =0; i<ippGetCount(attr); i++){
        const char *p = ippGetString(attr, i, NULL);
        if(strncasecmp(p, "RS", 2)) continue;
        int lo; int hi;
        lo = atoi(p+2);
        if(lo==0) lo = -1;
        p = strchr(p, '-');
        if(p) hi = atoi(p+1);
        else hi = lo;
        xres = hi;
        yres = hi;
      }
    }
  }

  if (log) {
    if (*cspace == -1)
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "Color space requested: Default");
    else
      log(ld, FILTER_LOGLEVEL_DEBUG,
	  "Color space requested: #%d", *cspace);
    log(ld, FILTER_LOGLEVEL_DEBUG,
	"Final output format: %s Raster",
	appleraster ? "Apple" : (cupsraster ? "CUPS" : "PWG"));
  }

  if (ppd)
  {
    if (log)
      log(ld, FILTER_LOGLEVEL_DEBUG, "PPD file present");
    ppdRasterInterpretPPD(h, ppd, num_options, options, 0);
    if ((ppd_attr = ppdFindAttr(ppd,"PWGRaster",0)) != 0 &&
	(!strcasecmp(ppd_attr->value, "true") ||
	 !strcasecmp(ppd_attr->value, "on") ||
	 !strcasecmp(ppd_attr->value, "yes"))) {
      pwgraster = 1;
      cupsraster = 0;
      appleraster = 0;
      if (log)
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "PWG Raster output requested (via \"PWGRaster\" PPD attribute)");
    }
    if (pwgraster || appleraster) {
      cupsRasterParseIPPOptions(h, data, pwgraster, 0);
      if ((pwgraster &&
	   (ppd_attr = ppdFindAttr(ppd, "cupsPwgRasterDocumentTypeSupported",
				   NULL)) != NULL) ||
	  (appleraster &&
	   (ppd_attr = ppdFindAttr(ppd, "cupsUrfSupported", NULL)) != NULL)) {
	cspaces_available = ppd_attr->value;
	if (cspaces_available && cspaces_available[0]) {
	  /* PPD is from the PPD generator (cfCreatePPDFromIPP()) of
	     cups-filters, so we auto-select color space and depth */
	  if ((color_mode = cupsGetOption("print-color-mode", num_options,
					  options)) == NULL) {
	    choice = ppdFindMarkedChoice(ppd, "ColorModel");
	    if (choice)
	      color_mode = choice->choice;
	    else
	      color_mode = "auto";
	  }
	  if ((quality = cupsGetOption("print-quality", num_options,
				       options)) == NULL) {
	    choice = ppdFindMarkedChoice(ppd, "cupsPrintQuality");
	    if (choice)
	      quality = choice->choice;
	    else
	      quality = "Normal";
	  }
	  hi_depth = (!strcasecmp(quality, "High") || !strcmp(quality, "5")) ?
	    1 : 0;
	  if (log) {
	    log(ld, FILTER_LOGLEVEL_DEBUG,
		"Color mode requested: %s; color depth requested: %s",
		color_mode, hi_depth ? "High" : "Standard");
	    log(ld, FILTER_LOGLEVEL_DEBUG,
		"Determining best color space/depth ...");
	  }
	  res = cupsRasterSetColorSpace(h, cspaces_available, color_mode,
					cspace, &hi_depth);
	}
      }
    }
  }
  else
  {
    if (log)
      log(ld, FILTER_LOGLEVEL_DEBUG, "No PPD file present");
    if (cupsraster)
    {
      pwgraster = 0;
      p = cupsGetOption("media-class", num_options, options);
      if (p == NULL)
	p = cupsGetOption("MediaClass", num_options, options);
      if (p != NULL)
      {
	if (strcasestr(p, "pwg")) {
	  pwgraster = 1;
	  cupsraster = 0;
	  if (log)
	    log(ld, FILTER_LOGLEVEL_DEBUG,
		"PWG Raster output requested (via \"MediaClass\"/\"media-class\" option)");
	} else
	  pwgraster = 0;
      }
    }
    cupsRasterParseIPPOptions(h, data, pwgraster, 1);
    if (printer_attrs &&
	((pwgraster &&
	  (attr = ippFindAttribute(printer_attrs,
				   "pwg-raster-document-type-supported",
				   IPP_TAG_ZERO)) != NULL) ||
	 (appleraster &&
	  (attr = ippFindAttribute(printer_attrs,
				   "urf-supported",
				   IPP_TAG_ZERO)) != NULL))) {
      ippAttributeString(attr, valuebuffer, sizeof(valuebuffer));
      cspaces_available = valuebuffer;
      if ((color_mode = cupsGetOption("print-color-mode", num_options,
				      options)) == NULL)
	color_mode = ippAttrEnumValForPrinter(printer_attrs, job_attrs,
					      "print-color-mode");
      if ((quality = cupsGetOption("print-quality", num_options,
				   options)) == NULL)
	quality = ippAttrEnumValForPrinter(printer_attrs, job_attrs,
					   "print-quality");
      hi_depth = (!strcasecmp(quality, "high") || !strcmp(quality, "5")) ?
	1 : 0;
      if (log) {
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "Color mode requested: %s; color depth requested: %s",
	    color_mode, hi_depth ? "High" : "Standard");
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "Determining best color space/depth ...");
      }
      res = cupsRasterSetColorSpace(h, cspaces_available, color_mode,
				    cspace, &hi_depth);
    }
  }

  if (res != 1) {
    /* cupsRasterSetColorSpace() was called */
    if (res < 0) {
      /* failed */
      if (log) {
	log(ld, FILTER_LOGLEVEL_ERROR,
	    "Unable to set color space/depth for Raster output!");
	if (*cspace < 0)
	  log(ld, FILTER_LOGLEVEL_ERROR,
	      "Did not find a valid color space!");
	else
	  log(ld, FILTER_LOGLEVEL_ERROR,
	      "Requested color space #%d not a valid PWG/Apple Raster color space!",
	      *cspace);
      }
      return (-1);
    } else
      /* succeeded */
      if (log)
	log(ld, FILTER_LOGLEVEL_DEBUG,
	    "Using color space #%d with %s color depth",
	    *cspace, hi_depth ? "high" : "standard");
  }

  if ((h->HWResolution[0] == 100) && (h->HWResolution[1] == 100)) {
    /* No "Resolution" option */
    if (ppd && (ppd_attr = ppdFindAttr(ppd, "DefaultResolution", 0)) != NULL) {
      /* "*DefaultResolution" keyword in the PPD */
      const char *p = ppd_attr->value;
      h->HWResolution[0] = atoi(p);
      if ((p = strchr(p, 'x')) != NULL)
	h->HWResolution[1] = atoi(p+1);   /*  Since p now points to a pointer such that *p = 'x', 
                                        therefore using p+1, cause p+1 points to a numeric value  */
      else
	h->HWResolution[1] = h->HWResolution[0];
      if (h->HWResolution[0] <= 0)
	h->HWResolution[0] = 300;
      if (h->HWResolution[1] <= 0)
	h->HWResolution[1] = h->HWResolution[0];
  } else {
    if(xres!=-1){
      h->HWResolution[0] = xres;
      h->HWResolution[1] = yres;  
    }
    else{
      h->HWResolution[0] = 300;
      h->HWResolution[1] = 300;
    }
  }
    h->cupsWidth = h->HWResolution[0] * h->PageSize[0] / 72;
    h->cupsHeight = h->HWResolution[1] * h->PageSize[1] / 72;
  }

  cupsFreeOptions(num_options, options);

  return (0);
}


/*
 * 'cupsRasterSetColorSpace() - Update a given CUPS/PWG Raster header to
 *                              the desired color mode, color space, and
 *                              color depth. We supply one of the printer
 *                              IPP attributes urf-supported or
 *                              pwg-raster-document-type-supported as they
 *                              contain a list of all valid combos of
 *                              color space and color depth supported by
 *                              the printer, tell the print-color-mode
 *                              attribute setting for this job and request
 *                              a color space and optionally high color
 *                              depth. Then it is checked first whether the
 *                              requested color space is available and if
 *                              not we fall back to the base color space
 *                              (usually sGray or sRGB). Then knowing the
 *                              color space we will use, we check whether in
 *                              this color space more than one color depth is
 *                              supported, we chooce the lowest, and if
 *                              high color depth is requested, the highest.
 */

int                                             /* O  - 0 on success,
						        -1 on error */
cupsRasterSetColorSpace(cups_page_header2_t *h, /* I  - Raster header */
			const char *available,  /* I  - Available color spaces
						        IPP attribute
						        urf-supported or
					   pwg-raster-document-type-supported */
			const char *color_mode, /* I  - print-color-mode IPP
						        attribute setting */
			cups_cspace_t *cspace,  /* IO - Color space we want to
						        use, -1 for auto, we
							return color space
							actually used, -1 if
						        no suitable color space
						        found. */
			int *high_depth)        /* IO - Do we want to print in
						        high color depth? We
						        reset to 0 if high
						        quality not supported
						        in the color space
						        used. */
{
  int min_depth = 999;
  int max_depth = 0;
  int best_depth = -1;
  int num_colors;
  int cspace_fallback = 0; /* 0: originally requested color space
			      1: sRGB for color, sGray for mono
			      2: sRGB for mono */
  const char *p, *q;

  /* Range-check */
  if (!h || !available || !cspace)
    return -1;

  if (*cspace != -1 && *cspace != CUPS_CSPACE_SW &&
      *cspace != CUPS_CSPACE_SRGB && *cspace != CUPS_CSPACE_ADOBERGB &&
      *cspace != CUPS_CSPACE_W && *cspace != CUPS_CSPACE_K &&
      *cspace != CUPS_CSPACE_RGB && *cspace != CUPS_CSPACE_CMYK)
    return (-1);

  /* True Bi-Di only available in PWG Raster.
     List of properties in pwg-raster-document-type-supported IPP attribute
     is lower-case-only whereas urf-supported for Apple Raster is upper-case-
     only */
  if (islower(available[0]) &&
      (p = strstr(available, "black_1")) != NULL &&
      !isdigit(p + 7) &&
      (!strcmp(color_mode, "bi-level") ||
       !strcmp(color_mode, "process-bi-level"))) {
    /* Set parameters for bi-level, there is only one color space and color
       depth */
    *cspace = CUPS_CSPACE_K;
    best_depth = 1;
    num_colors = 1;
    /* No high color depth in bi-level */
    if (high_depth)
      *high_depth = 0;
  } else {
    /* Any other color space */
    for (;;) { /* Loop through fallbacks to default if requested color space
		  not supported */
      if (*cspace >= 0) { /* Skip if no color space specified */
	for (p = available; p; p = q) {
	  int n, dmin, dmax;
	  /* Check whether requested color space is supported */
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
	       (num_colors = 4))) {
	    /* Color space supported, check color depth values */
	    n = sscanf(q, "%d-%d", &dmin, &dmax);
	    if (isupper(available[0]))
	      /* urf-supported specifies bits per pixel, we need bits per
		 color */
	      dmin = dmin / num_colors;
	    if (dmin < min_depth)
	      min_depth = dmin;
	    if (n == 2) {
	      if (isupper(available[0]))
		/* urf-supported specifies bits per pixel, we need bits per
		   color */
		dmax = dmax / num_colors;
	      if (dmax > max_depth)
		max_depth = dmax;
	    } else {
	      if (dmin > max_depth)
		max_depth = dmin;
	    }
	    /* Select depth depending on whether we want to have high or
	       standard color depth */
	    if (high_depth && *high_depth)
	      best_depth = max_depth;
	    else
	      best_depth = min_depth;
	  } else
	    /* No more entry for the requested color space in the attribute
	       string, quit loop */
	    break;
	}
	if (best_depth > 0) {
	  /* The requested color space is supported, so quit the fallback
	     loop */
	  if (high_depth && *high_depth && min_depth == max_depth)
	    /* We requested high color depth but our color space is only
	       supported with a single color depth, reset request to tell
	       that we did not find a higher color depth */
	    *high_depth = 0;
	  break;
	}
      }
      /* Arrived here, the requested color depth is not supported, try next
	 fallback level */
      cspace_fallback ++;
      if (cspace_fallback > 2) {
	/* Gone through all fallbacks and still no suitable color space?
	   Quit finally */
	*cspace = -1;
	return (-1);
      }
      /* Fallback 1: sRGB if we print in color and sGray if we print monochrome
         Fallback 2: sRGB always (if printer does not advertise mono mode)
         AdobeRGB instead of sRGB only if available in 16 bit per color and
         high color depth is requested */
      if ((cspace_fallback == 1 &&
	   (!strcasecmp(color_mode, "auto") ||
	    !strcasecmp(color_mode, "color"))) ||
	  cspace_fallback == 2) {
	if (high_depth && *high_depth &&
	    (strstr(available, "ADOBERGB24-48") ||
	     strstr(available, "ADOBERGB48") ||
	     strstr(available, "adobe-rgb_16")))
	  *cspace = CUPS_CSPACE_ADOBERGB;
	else
	  *cspace = CUPS_CSPACE_SRGB;
      } else
	*cspace = CUPS_CSPACE_SW;
    }
  }

  /* Success, update the raster header */
  h->cupsBitsPerColor = best_depth;
  h->cupsBitsPerPixel = best_depth * num_colors;
  h->cupsColorSpace = *cspace;
  h->cupsNumColors = num_colors;
  h->cupsBytesPerLine = (h->cupsWidth * h->cupsBitsPerPixel + 7) / 8;

  return (0);
}


/*
 * 'cupsRasterParseIPPOptions()' - Parse IPP options from the command line
 *                                 and apply them to the CUPS Raster header.
 */

int                                          /* O - -1 on error, 0 on success */
cupsRasterParseIPPOptions(cups_page_header2_t *h, /* I - Raster header */
			  filter_data_t *data,
        int pwg_raster,         /* I - 1 if PWG Raster */
			  int set_defaults)       /* I - If 1, se default values
						     for all fields for which
						     we did not get an option */
{
#ifdef HAVE_CUPS_1_7
  int		i;			/* Looping var */
  char		*ptr,			/* Pointer into string */
		s[255];			/* Temporary string */
  const char	*val,			/* Pointer into value */
                *media;			/* media option */
  char		*page_size,		/* PageSize option */
                *media_source,          /* Media source */
                *media_type;		/* Media type */
  pwg_media_t   *size_found;            /* page size found for given name */
  float         size;                   /* page size dimension */
  int num_options = 0;          /*  number of options */
  cups_option_t *options = NULL;  /*  Options */

 /*
  * Range check input...
  */

  if (!h)
    return (-1);

 /*
  * Check if the supplied "media" option is a comma-separated list of any
  * combination of page size ("media"), media source ("media-position"),
  * and media type ("media-type") and if so, put these list elements into
  * their dedicated options.
  */

  num_options = joinJobOptionsAndAttrs(data, num_options, &options);

  page_size = NULL;
  media_source = NULL;
  media_type = NULL;
  if ((media = cupsGetOption("media", num_options, options)) != NULL)
  {
   /*
    * Loop through the option string, separating it at commas and marking each
    * individual option as long as the corresponding PPD option (PageSize,
    * InputSlot, etc.) is not also set.
    *
    * For PageSize, we also check for an empty option value since some versions
    * of MacOS X use it to specify auto-selection of the media based solely on
    * the size.
    */

    for (val = media; *val;)
    {
     /*
      * Extract the sub-option from the string...
      */

      for (ptr = s; *val && *val != ',' && (ptr - s) < (sizeof(s) - 1);)
	*ptr++ = *val++;
      *ptr++ = '\0';

      if (*val == ',')
	val ++;

     /*
      * Identify it...
      */

      size_found = NULL;
      if ((size_found = pwgMediaForPWG(s)) == NULL)
	if ((size_found = pwgMediaForPPD(s)) == NULL)
	  if ((size_found = pwgMediaForPPD(s)) == NULL)
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
		strcasestr(s, "main"))
            { 
              if (media_source == NULL)
	        media_source = strdup(s);
            }
	    else
	      media_type = strdup(s);
	  }
      if (page_size == NULL && size_found)
	page_size = strdup(size_found->pwg);
    }
  }

  if (pwg_raster)
    strcpy(h->MediaClass, "PwgRaster");
  else if ((val = cupsGetOption("media-class", num_options, options)) != NULL ||
	   (val = cupsGetOption("MediaClass", num_options, options)) != NULL)
    _strlcpy(h->MediaClass, val, sizeof(h->MediaClass));
  else if (set_defaults)
    strcpy(h->MediaClass, "");

  if ((val = cupsGetOption("media-color", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaColor", num_options, options)) != NULL)
    _strlcpy(h->MediaColor, val, sizeof(h->MediaColor));
  else if (set_defaults)
    h->MediaColor[0] = '\0';

  if ((val = cupsGetOption("media-type", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaType", num_options, options)) != NULL ||
      (val = media_type) != NULL)
    _strlcpy(h->MediaType, val, sizeof(h->MediaType));
  else if (set_defaults)
    h->MediaType[0] = '\0';

  if ((val = cupsGetOption("print-content-optimize", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("output-type", num_options, options)) != NULL ||
      (val = cupsGetOption("OutputType", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "automatic"))
      _strlcpy(h->OutputType, "Automatic",
	      sizeof(h->OutputType));
    else if (!strcasecmp(val, "graphics"))
      _strlcpy(h->OutputType, "Graphics", sizeof(h->OutputType));
    else if (!strcasecmp(val, "photo"))
      _strlcpy(h->OutputType, "Photo", sizeof(h->OutputType));
    else if (!strcasecmp(val, "text"))
      _strlcpy(h->OutputType, "Text", sizeof(h->OutputType));
    else if (!strcasecmp(val, "text-and-graphics") ||
	     !strcasecmp(val, "TextAndGraphics"))
      _strlcpy(h->OutputType, "TextAndGraphics",
	      sizeof(h->OutputType));
    else if (pwg_raster)
      fprintf(stderr, "DEBUG: Unsupported print-content-type \"%s\".\n", val);
    else
      _strlcpy(h->OutputType, val, sizeof(h->OutputType));
  }
  else if (set_defaults)
    _strlcpy(h->OutputType, "Automatic", sizeof(h->OutputType));

  if (pwg_raster)
  {
    /* Set "reserved" fields to 0 */
    h->AdvanceDistance = 0;
    h->AdvanceMedia = CUPS_ADVANCE_NONE;
    h->Collate = CUPS_FALSE;
  }
  else
  {
    /* TODO - Support for advance distance and advance media */
    if (set_defaults)
    {
      h->AdvanceDistance = 0;
      h->AdvanceMedia = CUPS_ADVANCE_NONE;
    }
    if ((val = cupsGetOption("Collate", num_options, options)) != NULL &&
	(!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	 !strcasecmp(val, "yes")))
      h->Collate = CUPS_TRUE;
    else if ((val = cupsGetOption("Collate", num_options, options)) != NULL &&
	     (!strcasecmp(val, "false") || !strcasecmp(val, "off") ||
	      !strcasecmp(val, "no")))
      h->Collate = CUPS_FALSE;
    else if (set_defaults)
      h->Collate = CUPS_FALSE;
  }

  if (set_defaults)
    h->CutMedia = CUPS_CUT_NONE;

  if (set_defaults)
    h->Tumble = CUPS_FALSE;
  if ((val = cupsGetOption("sides", num_options, options)) != NULL ||
      (val = cupsGetOption("Duplex", num_options, options)) != NULL)
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
      if (!strncasecmp(val, "DuplexTumble", 12))
	h->Tumble = CUPS_TRUE;
      if (!strncasecmp(val, "DuplexNoTumble", 12))
	h->Tumble = CUPS_FALSE;
    }
    else if (set_defaults)
      h->Duplex = CUPS_FALSE;
  }
  else if (set_defaults)
    h->Duplex = CUPS_FALSE;

  if ((val = cupsGetOption("printer-resolution", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("Resolution", num_options, options)) != NULL)
  {
    int	        xres,		/* X resolution */
                yres;		/* Y resolution */
    char	*ptr;		/* Pointer into value */

    xres = yres = strtol(val, (char **)&ptr, 10);
    if (ptr > val && xres > 0)
    {
      if (*ptr == 'x')
	yres = strtol(ptr + 1, (char **)&ptr, 10);
    }

    if (ptr <= val || xres <= 0 || yres <= 0 || !ptr ||
	(*ptr != '\0' &&
	 strcasecmp(ptr, "dpi") &&
	 strcasecmp(ptr, "dpc") &&
	 strcasecmp(ptr, "dpcm")))
    {
      fprintf(stderr, "DEBUG: Bad resolution value \"%s\".\n", val);
      if (set_defaults)
      {
	h->HWResolution[0] = 600;
	h->HWResolution[1] = 600;
      }
    }
    else
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
  }
  else if (set_defaults)
  {
    h->HWResolution[0] = 600;
    h->HWResolution[1] = 600;
  }
  
  if (set_defaults)
  {
    /* TODO - Support for insert sheets */
    h->InsertSheet = CUPS_FALSE;
  }

  if (set_defaults)
  {
    /* TODO - Support for jog */
    h->Jog = CUPS_JOG_NONE;
  }

  if ((val = cupsGetOption("feed-orientation", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("feed-direction", num_options, options)) != NULL ||
      (val = cupsGetOption("LeadingEdge", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "ShortEdgeFirst"))
      h->LeadingEdge = CUPS_EDGE_TOP;
    else if (!strcasecmp(val, "LongEdgeFirst"))
      h->LeadingEdge = CUPS_EDGE_RIGHT;
    else
      fprintf(stderr, "DEBUG: Unsupported feed-orientation \"%s\".\n", val);
  }
  else if (set_defaults)
    h->LeadingEdge = CUPS_EDGE_TOP;

  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for manual feed */
    h->ManualFeed = CUPS_FALSE;
  }

  if ((val = cupsGetOption("media-position", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaPosition", num_options, options)) != NULL ||
      (val = cupsGetOption("media-source", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaSource", num_options, options)) != NULL ||
      (val = cupsGetOption("InputSlot", num_options, options)) != NULL ||
      (val = media_source) != NULL)
  {
    if (!strncasecmp(val, "Auto", 4) ||
	!strncasecmp(val, "Default", 7))
      h->MediaPosition = 0;
    else if (!strcasecmp(val, "Main"))
      h->MediaPosition = 1;
    else if (!strcasecmp(val, "Alternate"))
      h->MediaPosition = 2;
    else if (!strcasecmp(val, "LargeCapacity"))
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
    else if (!strcasecmp(val, "MainRoll"))
      h->MediaPosition = 9;
    else if (!strcasecmp(val, "AlternateRoll"))
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
    else if (!strcasecmp(val, "ByPassTray"))
      h->MediaPosition = 19;
    else if (!strcasecmp(val, "Tray1"))
      h->MediaPosition = 20;
    else if (!strcasecmp(val, "Tray2"))
      h->MediaPosition = 21;
    else if (!strcasecmp(val, "Tray3"))
      h->MediaPosition = 22;
    else if (!strcasecmp(val, "Tray4"))
      h->MediaPosition = 23;
    else if (!strcasecmp(val, "Tray5"))
      h->MediaPosition = 24;
    else if (!strcasecmp(val, "Tray6"))
      h->MediaPosition = 25;
    else if (!strcasecmp(val, "Tray7"))
      h->MediaPosition = 26;
    else if (!strcasecmp(val, "Tray8"))
      h->MediaPosition = 27;
    else if (!strcasecmp(val, "Tray9"))
      h->MediaPosition = 28;
    else if (!strcasecmp(val, "Tray10"))
      h->MediaPosition = 29;
    else if (!strcasecmp(val, "Tray11"))
      h->MediaPosition = 30;
    else if (!strcasecmp(val, "Tray12"))
      h->MediaPosition = 31;
    else if (!strcasecmp(val, "Tray13"))
      h->MediaPosition = 32;
    else if (!strcasecmp(val, "Tray14"))
      h->MediaPosition = 33;
    else if (!strcasecmp(val, "Tray15"))
      h->MediaPosition = 34;
    else if (!strcasecmp(val, "Tray16"))
      h->MediaPosition = 35;
    else if (!strcasecmp(val, "Tray17"))
      h->MediaPosition = 36;
    else if (!strcasecmp(val, "Tray18"))
      h->MediaPosition = 37;
    else if (!strcasecmp(val, "Tray19"))
      h->MediaPosition = 38;
    else if (!strcasecmp(val, "Tray20"))
      h->MediaPosition = 39;
    else if (!strcasecmp(val, "Roll1"))
      h->MediaPosition = 40;
    else if (!strcasecmp(val, "Roll2"))
      h->MediaPosition = 41;
    else if (!strcasecmp(val, "Roll3"))
      h->MediaPosition = 42;
    else if (!strcasecmp(val, "Roll4"))
      h->MediaPosition = 43;
    else if (!strcasecmp(val, "Roll5"))
      h->MediaPosition = 44;
    else if (!strcasecmp(val, "Roll6"))
      h->MediaPosition = 45;
    else if (!strcasecmp(val, "Roll7"))
      h->MediaPosition = 46;
    else if (!strcasecmp(val, "Roll8"))
      h->MediaPosition = 47;
    else if (!strcasecmp(val, "Roll9"))
      h->MediaPosition = 48;
    else if (!strcasecmp(val, "Roll10"))
      h->MediaPosition = 49;
    else
      fprintf(stderr, "DEBUG: Unsupported media source \"%s\".\n", val);
  }
  else if (set_defaults)
    h->MediaPosition = 0; /* Auto */

  if ((val = cupsGetOption("media-weight", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaWeight", num_options, options)) != NULL ||
      (val = cupsGetOption("media-weight-metric", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("MediaWeightMetric", num_options, options)) != NULL)
    h->MediaWeight = atol(val);
  else if (set_defaults)
    h->MediaWeight = 0;

  if (pwg_raster)
  {
    /* Set "reserved" fields to 0 */
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
      else if (set_defaults)
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
      else if (set_defaults)
	h->NegativePrint = CUPS_FALSE;
    }
  }

  if ((val = cupsGetOption("copies", num_options, options)) != NULL ||
      (val = cupsGetOption("Copies", num_options, options)) != NULL ||
      (val = cupsGetOption("num-copies", num_options, options)) != NULL ||
      (val = cupsGetOption("NumCopies", num_options, options)) != NULL)
    h->NumCopies = atol(val);
  else if (set_defaults)
    h->NumCopies = 1; /* 0 = Printer default */

  if ((val = cupsGetOption("orientation-requested", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("OrientationRequested", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("Orientation", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "Portrait") ||
	!strcasecmp(val, "3"))
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
      fprintf(stderr, "DEBUG: Unsupported Orientation \"%s\".\n", val);
  }
  else if (set_defaults)
    h->Orientation = CUPS_ORIENT_0;

  if (pwg_raster)
  {
    /* Set "reserved" fields to 0 */
    h->OutputFaceUp = CUPS_FALSE;
  }
  else
  {
    if ((val = cupsGetOption("OutputFaceUp", num_options, options)) != NULL ||
	(val = cupsGetOption("output-face-up", num_options, options)) != NULL)
    {
      if (!strcasecmp(val, "true") || !strcasecmp(val, "on") ||
	  !strcasecmp(val, "yes"))
	h->OutputFaceUp = CUPS_TRUE;
      else if (!strcasecmp(val, "false") ||
	       !strcasecmp(val, "off") ||
	       !strcasecmp(val, "no"))
	h->OutputFaceUp = CUPS_FALSE;
      else if (set_defaults)
	h->OutputFaceUp = CUPS_FALSE;
    }
  }

  if ((val = cupsGetOption("media-size", num_options, options)) != NULL ||
      (val = cupsGetOption("MediaSize", num_options, options)) != NULL ||
      (val = cupsGetOption("page-size", num_options, options)) != NULL ||
      (val = cupsGetOption("PageSize", num_options, options)) != NULL ||
      (val = page_size) != NULL)
  {
    size_found = NULL;
    if ((size_found = pwgMediaForPWG(val)) == NULL)
      if ((size_found = pwgMediaForPPD(val)) == NULL)
	size_found = pwgMediaForLegacy(val);
    if (size_found != NULL)
    {
      h->PageSize[0] = size_found->width * 72 / 2540;
      h->PageSize[1] = size_found->length * 72 / 2540;
      _strlcpy(h->cupsPageSizeName, size_found->pwg,
	      sizeof(h->cupsPageSizeName));
      if (pwg_raster)
      {
	h->cupsPageSize[0] = 0.0;
	h->cupsPageSize[1] = 0.0;
      }
      else
      {
	h->cupsPageSize[0] = size_found->width * 72.0 / 2540.0;
	h->cupsPageSize[1] = size_found->length * 72.0 / 2540.0;
      }
    }
    else
      fprintf(stderr, "DEBUG: Unsupported page size %s.\n", val);
  }
  else if (set_defaults)
  {
    /* TODO: Automatic A4/Letter, like in scheduler/conf.c in CUPS. */
    h->cupsPageSize[0] = 612.0f;
    h->cupsPageSize[1] = 792.0f;
    
    h->PageSize[0] = 612;
    h->PageSize[1] = 792;
    _strlcpy(h->cupsPageSizeName, "na_letter_8.5x11in",
	    sizeof(h->cupsPageSizeName));
    if (pwg_raster)
    {
      h->cupsPageSize[0] = 0.0;
      h->cupsPageSize[1] = 0.0;
    }
  }
  else if (pwg_raster)
  {
    h->cupsPageSize[0] = 0.0;
    h->cupsPageSize[1] = 0.0;
  }

  if (pwg_raster)
  {
    /* Set "reserved" fields to 0 */
    h->Margins[0] = 0;
    h->Margins[1] = 0;
    h->ImagingBoundingBox[0] = 0;
    h->ImagingBoundingBox[1] = 0;
    h->ImagingBoundingBox[2] = 0;
    h->ImagingBoundingBox[3] = 0;
    h->cupsImagingBBox[0] = 0.0;
    h->cupsImagingBBox[1] = 0.0;
    h->cupsImagingBBox[2] = 0.0;
    h->cupsImagingBBox[3] = 0.0;
  }
  else
  {
    if ((val = cupsGetOption("media-left-margin", num_options, options))
	!= NULL)
    {
      size = atol(val) * 72.0 / 2540.0; 
      h->Margins[0] = (int)size;
      h->ImagingBoundingBox[0] = (int)size;
      h->cupsImagingBBox[0] = size;
    }
    else if (set_defaults)
    {
      h->Margins[0] = 0;
      h->ImagingBoundingBox[0] = 0;
      h->cupsImagingBBox[0] = 18.0f;
    }
    if ((val = cupsGetOption("media-bottom-margin", num_options, options))
	!= NULL)
    {
      size = atol(val) * 72.0 / 2540.0; 
      h->Margins[1] = (int)size;
      h->ImagingBoundingBox[1] = (int)size;
      h->cupsImagingBBox[1] = size;
    }
    else if (set_defaults)
    {
      h->Margins[1] = 0;
      h->ImagingBoundingBox[1] = 0;
      h->cupsImagingBBox[1] = 36.0f;
    }
    if ((val = cupsGetOption("media-right-margin", num_options, options))
	!= NULL)
    {
      size = atol(val) * 72.0 / 2540.0; 
      h->ImagingBoundingBox[2] = h->PageSize[0] - (int)size;
      h->cupsImagingBBox[2] = h->cupsPageSize[0] - size;
    }
    else if (set_defaults)
    {
      h->ImagingBoundingBox[2] = h->PageSize[0];
      h->cupsImagingBBox[2] = 594.0f;
    }
    if ((val = cupsGetOption("media-top-margin", num_options, options))
	!= NULL)
    {
      size = atol(val) * 72.0 / 2540.0; 
      h->ImagingBoundingBox[3] = h->PageSize[1] - (int)size;
      h->cupsImagingBBox[3] = h->cupsPageSize[1] - size;
    }
    else if (set_defaults)
    {
      h->ImagingBoundingBox[3] = h->PageSize[1];
      h->cupsImagingBBox[3] = 756.0f;
    }
  }

  if (pwg_raster)
  {
    /* Set "reserved" fields to 0 */
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
      else if (set_defaults)
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
      else if (set_defaults)
	h->TraySwitch = CUPS_FALSE;
    }
  }

  if ((val = cupsGetOption("sides", num_options, options)) != NULL ||
      (val = cupsGetOption("Tumble", num_options, options)) != NULL)
  {
    if (!strcasecmp(val, "None") || !strcasecmp(val, "Off") ||
	!strcasecmp(val, "False") || !strcasecmp(val, "No") ||
	!strcasecmp(val, "one-sided") || !strcasecmp(val, "OneSided") ||
	!strcasecmp(val, "two-sided-long-edge") ||
	!strcasecmp(val, "TwoSidedLongEdge") ||
	!strcasecmp(val, "DuplexNoTumble"))
      h->Tumble = CUPS_FALSE;
    else if (!strcasecmp(val, "On") ||
	     !strcasecmp(val, "True") || !strcasecmp(val, "Yes") ||
	     !strcasecmp(val, "two-sided-short-edge") ||
	     !strcasecmp(val, "TwoSidedShortEdge") ||
	     !strcasecmp(val, "DuplexTumble"))
      h->Tumble = CUPS_TRUE;
  }

  h->cupsWidth = h->HWResolution[0] * h->PageSize[0] / 72;
  h->cupsHeight = h->HWResolution[1] * h->PageSize[1] / 72;

  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for MediaType number */
    h->cupsMediaType = 0;
  }

  if ((val = cupsGetOption("pwg-raster-document-type", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PwgRasterDocumentType", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("color-space", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorSpace", num_options, options)) != NULL ||
      (val = cupsGetOption("color-model", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorModel", num_options, options)) != NULL ||
      (val = cupsGetOption("print-color-mode", num_options, options)) != NULL ||
      (val = cupsGetOption("output-mode", num_options, options)) != NULL ||
      (val = cupsGetOption("OutputMode", num_options, options)) != NULL)
  {
    int	        bitspercolor,	/* Bits per color */
                bitsperpixel,   /* Bits per pixel */
                colorspace,     /* CUPS/PWG raster color space */
                numcolors;	/* Number of colorants */
    const char	*ptr;		/* Pointer into value */

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
      /* Let "auto" not look like an error */
      if (set_defaults)
      {
	fprintf(stderr,
		"DEBUG: \"Auto\" mode, using default RGB color space.\n");
	colorspace = 19;
	numcolors = 3;
      }
    }
    if (numcolors > 0)
    {
      if (ptr)
	bitspercolor = strtol(ptr, (char **)&ptr, 10);
      bitsperpixel = bitspercolor * numcolors;
      /* In 1-bit-per-color RGB modes we add a forth bit to each pixel
	 to align the pixels with bytes */
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
      fprintf(stderr, "DEBUG: Bad color space value \"%s\".\n", val);
      if (set_defaults)
      {
	h->cupsBitsPerColor = 8;
	h->cupsBitsPerPixel = 24;
	h->cupsColorSpace = 19;
	h->cupsNumColors = 3;
      }
    }
  }
  else if (set_defaults)
  {
    h->cupsBitsPerColor = 8;
    h->cupsBitsPerPixel = 24;
    h->cupsColorSpace = 19;
    h->cupsNumColors = 3;
  }

  h->cupsBytesPerLine = (h->cupsWidth * h->cupsBitsPerPixel + 7) / 8;
  
  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for color orders 1 (banded) and 2 (planar) */
    h->cupsColorOrder = 0;
  }
  
  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for these parameters */
    h->cupsCompression = 0;
    h->cupsRowCount = 0;
    h->cupsRowFeed = 0;
    h->cupsRowStep = 0;
  }

  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for cupsBorderlessScalingFactor */
    h->cupsBorderlessScalingFactor = 0.0;
  }

  if (pwg_raster || set_defaults)
  {
    /* TODO - Support for custom values in CUPS Raster mode */
    for (i = 0; i < 16; i ++)
    {
      h->cupsInteger[i] = 0;
      h->cupsReal[i] = 0.0;
      memset(h->cupsString[i], 0, 64);
    }
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

    if ((val = cupsGetOption("pwg-raster-document-sheet-back", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("PwgRasterDocumentSheetBack", num_options,
			     options)) != NULL)
    {
      /* Set CrossFeedTransform and FeedTransform */
      if (h->Duplex == CUPS_FALSE)
      {
	h->cupsInteger[1] = 1;
	h->cupsInteger[2] = 1;
      }
      else if (h->Duplex == CUPS_TRUE)
      {
	if (h->Tumble == CUPS_FALSE)
	{
	  if (!strcasecmp(val, "Flipped"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] = -1;
	  }
	  else if (!strncasecmp(val, "Manual", 6))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Normal"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Rotated"))
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
	  if (!strcasecmp(val, "Flipped"))
	  {
	    h->cupsInteger[1] = -1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strncasecmp(val, "Manual", 6))
	  {
	    h->cupsInteger[1] = -1;
	    h->cupsInteger[2] = -1;
	  }
	  else if (!strcasecmp(val, "Normal"))
	  {
	    h->cupsInteger[1] =  1;
	    h->cupsInteger[2] =  1;
	  }
	  else if (!strcasecmp(val, "Rotated"))
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

    /* TODO - Support for ImageBoxLeft, ImageBoxTop, ImageBoxRight, and
       ImageBoxBottom (h->cupsInteger[3..6]), leave on 0 for now */

    if ((val = cupsGetOption("alternate-primary", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("AlternatePrimary", num_options,
			     options)) != NULL)
    {
      int alternateprimary = atoi(val);		/* SRGB value for black
						   pixels */
      h->cupsInteger[7] = alternateprimary;
    }

    if ((val = cupsGetOption("print-quality", num_options, options)) != NULL ||
	(val = cupsGetOption("PrintQuality", num_options, options)) != NULL ||
	(val = cupsGetOption("Quality", num_options, options)) != NULL)
    {
      int quality = atoi(val);		/* print-quality value */

      if (!quality ||
	  (quality >= IPP_QUALITY_DRAFT && quality <= IPP_QUALITY_HIGH))
	h->cupsInteger[8] = quality;
      else
	fprintf(stderr, "DEBUG: Unsupported print-quality %d.\n", quality);
    }

    /* Leave "reserved" fields (h->cupsInteger[9..13]) on 0 */

    if ((val = cupsGetOption("vendor-identifier", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("VendorIdentifier", num_options,
			     options)) != NULL)
    {
      int vendorid = atoi(val);		/* USB ID of manufacturer */
      h->cupsInteger[14] = vendorid;
    }

    if ((val = cupsGetOption("vendor-length", num_options,
			     options)) != NULL ||
	(val = cupsGetOption("VendorLength", num_options,
			     options)) != NULL)
    {
      int vendorlength = atoi(val);		/* How many bytes of vendor
						   data? */
      if (vendorlength <= 1088)
      {
	h->cupsInteger[15] = vendorlength;
	if ((val = cupsGetOption("vendor-data", num_options,
				 options)) != NULL ||
	    (val = cupsGetOption("VendorData", num_options,
				 options)) != NULL)
	  /* TODO - How to enter binary data here? */
	  _strlcpy((char *)&(h->cupsReal[0]), val, 1088);
      }
    }
  }

  if (pwg_raster || set_defaults)
  {
    /* Set "reserved" fields to 0 */
    memset(h->cupsMarkerType, 0, 64);
  }

  if ((val = cupsGetOption("print-rendering-intent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PrintRenderingIntent", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("RenderingIntent", num_options,
			   options)) != NULL)
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
    else if (!strcmp(val, "relative-bpc"))
      _strlcpy(h->cupsRenderingIntent, "RelativeBpc",
	      sizeof(h->cupsRenderingIntent));
    else if (!strcmp(val, "saturation"))
      _strlcpy(h->cupsRenderingIntent, "Saturation",
	      sizeof(h->cupsRenderingIntent));
    else
      fprintf(stderr, "DEBUG: Unsupported print-rendering-intent \"%s\".\n",
	      val);
  }
  else if (set_defaults)
    h->cupsRenderingIntent[0] = '\0';
#endif /* HAVE_CUPS_1_7 */

  if (media_source != NULL)
    free(media_source);
  if (media_type != NULL)
    free(media_type);
  if (page_size != NULL)
    free(page_size);

  cupsFreeOptions(num_options, options);

  return (0);
}


/*  Function for storing job-attrs in options */
int joinJobOptionsAndAttrs(filter_data_t* data, int num_options, cups_option_t **options)
{
  ipp_t *job_attrs = data->job_attrs;   /*  Job attributes  */
  ipp_attribute_t *ipp_attr;            /*  IPP attribute   */
  int i = 0;                            /*  Looping variable*/
  char buf[2048];                       /*  Buffer for storing value of ipp attr  */
  cups_option_t *opt;

  for(i = 0, opt=data->options; i<data->num_options; i++, opt++){
    num_options = cupsAddOption(opt->name, opt->value, num_options, options);
  }

  for(ipp_attr = ippFirstAttribute(job_attrs); ipp_attr; ipp_attr = ippNextAttribute(job_attrs)){
    ippAttributeString(ipp_attr, buf, sizeof(buf));
    num_options = cupsAddOption(ippGetName(ipp_attr), buf, num_options, options);
  }

  return num_options;
}

/*
 * End
 */
