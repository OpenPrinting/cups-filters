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

#include "driver.h"
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
 * 'cupsRasterParseIPPOptions()' - Parse IPP options from the command line
 *                                 and apply them to the CUPS Raster header.
 */

int                                          /* O - -1 on error, 0 on success */
cupsRasterParseIPPOptions(cups_page_header2_t *h, /* I - Raster header */
			  int num_options,        /* I - Number of options */
			  cups_option_t *options, /* I - Options */
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
		*media,			/* media option */
		*page_size,		/* PageSize option */
                *media_source,          /* Media source */
                *media_type;		/* Media type */
  pwg_media_t   *size_found;            /* page size found for given name */
  float         size;                   /* page size dimension */

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
	      media_source = strdup(s);
	    else
	      media_type = strdup(s);
	  }
      if (size_found)
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
      h->cupsImagingBBox[0] = 0.0;
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
      h->cupsImagingBBox[1] = 0.0;
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
      h->cupsImagingBBox[2] = h->cupsPageSize[0];
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
      h->cupsImagingBBox[3] = h->cupsPageSize[1];
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
      (val = cupsGetOption("print-color-mode", num_options,
			   options)) != NULL ||
      (val = cupsGetOption("PrintColorMode", num_options, options)) != NULL ||
      (val = cupsGetOption("color-space", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorSpace", num_options, options)) != NULL ||
      (val = cupsGetOption("color-model", num_options, options)) != NULL ||
      (val = cupsGetOption("ColorModel", num_options, options)) != NULL)
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
    else if (!strncasecmp(val, "Black", 5))
    {
      if (*(val + 5) == '_' || *(val + 5) == '-') 
	ptr = val + 6;
      bitspercolor = 1;
      colorspace = 3;
      numcolors = 1;
    }
    else if (!strncasecmp(val, "Cmyk", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-') 
	ptr = val + 5;
      colorspace = 6;
      numcolors = 4;
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
    else if (!strncasecmp(val, "Srgb", 4))
    {
      if (*(val + 4) == '_' || *(val + 4) == '-') 
	ptr = val + 5;
      colorspace = 19;
      numcolors = 3;
    }
    else if (!strncasecmp(val, "Rgb", 3))
    {
      if (*(val + 3) == '_' || *(val + 3) == '-') 
	ptr = val + 4;
      colorspace = 1;
      numcolors = 3;
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
	h->cupsBitsPerColor = 1;
	h->cupsBitsPerPixel = 1;
	h->cupsColorSpace = 3;
	h->cupsNumColors = 1;
      }
    }
  }
  else if (set_defaults)
  {
    h->cupsBitsPerColor = 1;
    h->cupsBitsPerPixel = 1;
    h->cupsColorSpace = 3;
    h->cupsNumColors = 1;
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

  return (0);
}


/*
 * End
 */
