/*
 *   IPP Everywhere/Apple Raster/IPP legacy PPD generator
 *
 *   Copyright 2016 by Till Kamppeter.
 *
 *   The PPD generator is based on the PPD generator for the CUPS
 *   "lpadmin -m everywhere" functionality in the cups/ppd-cache.c
 *   file. The copyright of this file is:
 *
 *   Copyright 2010-2016 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "COPYING"
 *   which should have been included with this file. 
 *
 * Contents:
 *
 *   ppdCreateFromIPP() - Create a PPD file based on the result of an
 *                        get-printer-attributes IPP reuqst
 */

#include <config.h>
#include <cups/cups.h>
#if (CUPS_VERSION_MAJOR > 1) || (CUPS_VERSION_MINOR > 5)
#define HAVE_CUPS_1_6 1
#endif
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

#ifdef HAVE_CUPS_1_6
/* The following code uses a lot of CUPS >= 1.6 specific stuff.
   It needed for create_local_queue() in cups-browsed
   to set up local queues for non-CUPS printer broadcasts
   that is disabled in create_local_queue() for older CUPS <= 1.5.4.
   Accordingly the following code is also disabled here for CUPS < 1.6. */

/*
 * The code below is borrowed from the CUPS 2.2.x upstream repository
 * (via patches attached to https://www.cups.org/str.php?L4258). This
 * allows for automatic PPD generation already with CUPS versions older
 * than CUPS 2.2.x. We have also an additional test and development
 * platform for this code. Taken from cups/ppd-cache.c,
 * cups/string-private.h, cups/string.c.
 * 
 * The advantage of PPD generation instead of working with System V
 * interface scripts is that the print dialogs of the clients do not
 * need to ask the printer for its options via IPP. So we have access
 * to the options with the current PPD-based dialogs and can even share
 * the automatically created print queue to other CUPS-based machines
 * without problems.
 */


typedef struct _pwg_finishings_s	/**** PWG finishings mapping data ****/
{
  ipp_finishings_t	value;		/* finishings value */
  int			num_options;	/* Number of options to apply */
  cups_option_t		*options;	/* Options to apply */
} _pwg_finishings_t;

char ppdgenerator_msg[1024];

#define _PWG_EQUIVALENT(x, y)	(abs((x)-(y)) < 2)

static void	pwg_ppdize_name(const char *ipp, char *name, size_t namesize);
static void	pwg_ppdize_resolution(ipp_attribute_t *attr, int element, int *xres, int *yres, char *name, size_t namesize);

int			/* O - 1 on match, 0 otherwise */
_cups_isalnum(int ch)			/* I - Character to test */
{
  return ((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

int			/* O - 1 on match, 0 otherwise */
_cups_isalpha(int ch)			/* I - Character to test */
{
  return ((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

int			/* O - 1 on match, 0 otherwise */
_cups_islower(int ch)			/* I - Character to test */
{
  return (ch >= 'a' && ch <= 'z');
}

int			/* O - 1 on match, 0 otherwise */
_cups_isspace(int ch)			/* I - Character to test */
{
  return (ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' ||
          ch == '\v');
}

int			/* O - 1 on match, 0 otherwise */
_cups_isupper(int ch)			/* I - Character to test */
{
  return (ch >= 'A' && ch <= 'Z');
}

int			/* O - Converted character */
_cups_tolower(int ch)			/* I - Character to convert */
{
  return (_cups_isupper(ch) ? ch - 'A' + 'a' : ch);
}

int			/* O - Converted character */
_cups_toupper(int ch)			/* I - Character to convert */
{
  return (_cups_islower(ch) ? ch - 'a' + 'A' : ch);
}

#ifndef HAVE_STRLCPY
/*
 * '_cups_strlcpy()' - Safely copy two strings.
 */

size_t					/* O - Length of string */
strlcpy(char       *dst,		/* O - Destination string */
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

  memmove(dst, src, srclen);
  dst[srclen] = '\0';

  return (srclen);
}
#endif /* !HAVE_STRLCPY */

/*
 * '_cupsStrFormatd()' - Format a floating-point number.
 */

char *					/* O - Pointer to end of string */
_cupsStrFormatd(char         *buf,	/* I - String */
                char         *bufend,	/* I - End of string buffer */
		double       number,	/* I - Number to format */
                struct lconv *loc)	/* I - Locale data */
{
  char		*bufptr,		/* Pointer into buffer */
		temp[1024],		/* Temporary string */
		*tempdec,		/* Pointer to decimal point */
		*tempptr;		/* Pointer into temporary string */
  const char	*dec;			/* Decimal point */
  int		declen;			/* Length of decimal point */


 /*
  * Format the number using the "%.12f" format and then eliminate
  * unnecessary trailing 0's.
  */

  snprintf(temp, sizeof(temp), "%.12f", number);
  for (tempptr = temp + strlen(temp) - 1;
       tempptr > temp && *tempptr == '0';
       *tempptr-- = '\0');

 /*
  * Next, find the decimal point...
  */

  if (loc && loc->decimal_point)
  {
    dec    = loc->decimal_point;
    declen = (int)strlen(dec);
  }
  else
  {
    dec    = ".";
    declen = 1;
  }

  if (declen == 1)
    tempdec = strchr(temp, *dec);
  else
    tempdec = strstr(temp, dec);

 /*
  * Copy everything up to the decimal point...
  */

  if (tempdec)
  {
    for (tempptr = temp, bufptr = buf;
         tempptr < tempdec && bufptr < bufend;
	 *bufptr++ = *tempptr++);

    tempptr += declen;

    if (*tempptr && bufptr < bufend)
    {
      *bufptr++ = '.';

      while (*tempptr && bufptr < bufend)
        *bufptr++ = *tempptr++;
    }

    *bufptr = '\0';
  }
  else
  {
    strlcpy(buf, temp, (size_t)(bufend - buf + 1));
    bufptr = buf + strlen(buf);
  }

  return (bufptr);
}


/*
 * '_cups_strcasecmp()' - Do a case-insensitive comparison.
 */

int				/* O - Result of comparison (-1, 0, or 1) */
_cups_strcasecmp(const char *s,	/* I - First string */
                 const char *t)	/* I - Second string */
{
  while (*s != '\0' && *t != '\0')
  {
    if (_cups_tolower(*s) < _cups_tolower(*t))
      return (-1);
    else if (_cups_tolower(*s) > _cups_tolower(*t))
      return (1);

    s ++;
    t ++;
  }

  if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}

/*
 * '_cups_strncasecmp()' - Do a case-insensitive comparison on up to N chars.
 */

int					/* O - Result of comparison (-1, 0, or 1) */
_cups_strncasecmp(const char *s,	/* I - First string */
                  const char *t,	/* I - Second string */
		  size_t     n)		/* I - Maximum number of characters to compare */
{
  while (*s != '\0' && *t != '\0' && n > 0)
  {
    if (_cups_tolower(*s) < _cups_tolower(*t))
      return (-1);
    else if (_cups_tolower(*s) > _cups_tolower(*t))
      return (1);

    s ++;
    t ++;
    n --;
  }

  if (n == 0)
    return (0);
  else if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}

/*
 * '_()' - Simplify copying the ppdCreateFromIPP() function from CUPS,
 *         as we do not do translations of UI strings in cups-browsed
 */

#define _(s) s

/*
 * '_cupsLangString()' - Simplify copying the ppdCreateFromIPP() function
 *                       from CUPS, as we do not do translations of UI strings
 *                       in cups-browsed
 */

const char *
_cupsLangString(cups_lang_t *l, const char *s)
{
  return s;
}

/*
 * 'ppdCreateFromIPP()' - Create a PPD file describing the capabilities
 *                         of an IPP printer.
 */

char *					/* O - PPD filename or NULL on error */
ppdCreateFromIPP(char   *buffer,	/* I - Filename buffer */
		 size_t bufsize,	/* I - Size of filename buffer */
		 ipp_t  *response,	/* I - Get-Printer-Attributes response */
		 const char *make_model,/* I - Make and model from DNS-SD */
		 const char *pdl,       /* I - List of PDLs from DNS-SD */
		 int    color,          /* I - Color printer? (from DNS-SD) */
		 int    duplex)         /* O - Error/status message */
{
  cups_file_t		*fp;		/* PPD file */
  cups_array_t		*sizes;		/* Media sizes we've added */
  ipp_attribute_t	*attr,		/* xxx-supported */
			*defattr,	/* xxx-default */
			*x_dim, *y_dim;	/* Media dimensions */
  ipp_t			*media_size;	/* Media size collection */
  char			make[256],	/* Make and model */
			*model,		/* Model name */
			ppdname[PPD_MAX_NAME];
		    			/* PPD keyword */
  int			i, j,		/* Looping vars */
			count,		/* Number of values */
			bottom,		/* Largest bottom margin */
			left,		/* Largest left margin */
			right,		/* Largest right margin */
			top,		/* Largest top margin */
			is_apple = 0,	/* Does the printer support Apple raster? */
			is_pwg = 0;	/* Does the printer support PWG Raster? */
  pwg_media_t		*pwg;		/* PWG media size */
  int			xres, yres;	/* Resolution values */
  cups_lang_t		*lang = cupsLangDefault();
					/* Localization info */
  struct lconv		*loc = localeconv();
					/* Locale data */
  static const char * const finishings[][2] =
  {					/* Finishings strings */
    { "bale", _("Bale") },
    { "bind", _("Bind") },
    { "bind-bottom", _("Bind (Reverse Landscape)") },
    { "bind-left", _("Bind (Portrait)") },
    { "bind-right", _("Bind (Reverse Portrait)") },
    { "bind-top", _("Bind (Landscape)") },
    { "booklet-maker", _("Booklet Maker") },
    { "coat", _("Coat") },
    { "cover", _("Cover") },
    { "edge-stitch", _("Staple Edge") },
    { "edge-stitch-bottom", _("Staple Edge (Reverse Landscape)") },
    { "edge-stitch-left", _("Staple Edge (Portrait)") },
    { "edge-stitch-right", _("Staple Edge (Reverse Portrait)") },
    { "edge-stitch-top", _("Staple Edge (Landscape)") },
    { "fold", _("Fold") },
    { "fold-accordian", _("Accordian Fold") },
    { "fold-double-gate", _("Double Gate Fold") },
    { "fold-engineering-z", _("Engineering Z Fold") },
    { "fold-gate", _("Gate Fold") },
    { "fold-half", _("Half Fold") },
    { "fold-half-z", _("Half Z Fold") },
    { "fold-left-gate", _("Left Gate Fold") },
    { "fold-letter", _("Letter Fold") },
    { "fold-parallel", _("Parallel Fold") },
    { "fold-poster", _("Poster Fold") },
    { "fold-right-gate", _("Right Gate Fold") },
    { "fold-z", _("Z Fold") },
    { "jog-offset", _("Jog") },
    { "laminate", _("Laminate") },
    { "punch", _("Punch") },
    { "punch-bottom-left", _("Single Punch (Reverse Landscape)") },
    { "punch-bottom-right", _("Single Punch (Reverse Portrait)") },
    { "punch-double-bottom", _("2-Hole Punch (Reverse Portrait)") },
    { "punch-double-left", _("2-Hole Punch (Reverse Landscape)") },
    { "punch-double-right", _("2-Hole Punch (Landscape)") },
    { "punch-double-top", _("2-Hole Punch (Portrait)") },
    { "punch-quad-bottom", _("4-Hole Punch (Reverse Landscape)") },
    { "punch-quad-left", _("4-Hole Punch (Portrait)") },
    { "punch-quad-right", _("4-Hole Punch (Reverse Portrait)") },
    { "punch-quad-top", _("4-Hole Punch (Landscape)") },
    { "punch-top-left", _("Single Punch (Portrait)") },
    { "punch-top-right", _("Single Punch (Landscape)") },
    { "punch-triple-bottom", _("3-Hole Punch (Reverse Landscape)") },
    { "punch-triple-left", _("3-Hole Punch (Portrait)") },
    { "punch-triple-right", _("3-Hole Punch (Reverse Portrait)") },
    { "punch-triple-top", _("3-Hole Punch (Landscape)") },
    { "punch-multiple-bottom", _("Multi-Hole Punch (Reverse Landscape)") },
    { "punch-multiple-left", _("Multi-Hole Punch (Portrait)") },
    { "punch-multiple-right", _("Multi-Hole Punch (Reverse Portrait)") },
    { "punch-multiple-top", _("Multi-Hole Punch (Landscape)") },
    { "saddle-stitch", _("Saddle Stitch") },
    { "staple", _("Staple") },
    { "staple-bottom-left", _("Single Staple (Reverse Landscape)") },
    { "staple-bottom-right", _("Single Staple (Reverse Portrait)") },
    { "staple-dual-bottom", _("Double Staple (Reverse Landscape)") },
    { "staple-dual-left", _("Double Staple (Portrait)") },
    { "staple-dual-right", _("Double Staple (Reverse Portrait)") },
    { "staple-dual-top", _("Double Staple (Landscape)") },
    { "staple-top-left", _("Single Staple (Portrait)") },
    { "staple-top-right", _("Single Staple (Landscape)") },
    { "staple-triple-bottom", _("Triple Staple (Reverse Landscape)") },
    { "staple-triple-left", _("Triple Staple (Portrait)") },
    { "staple-triple-right", _("Triple Staple (Reverse Portrait)") },
    { "staple-triple-top", _("Triple Staple (Landscape)") },
    { "trim", _("Cut Media") }
  };
  char			filter_path[1024];
                                        /* Path to filter executable */
  const char		*cups_serverbin;/* CUPS_SERVERBIN environment
					   variable */

 /*
  * Range check input...
  */

  if (buffer)
    *buffer = '\0';

  if (!buffer || bufsize < 1 || !response)
    return (NULL);

 /*
  * Open a temporary file for the PPD...
  */

  if ((fp = cupsTempFile2(buffer, (int)bufsize)) == NULL)
    return (NULL);

 /*
  * Standard stuff for PPD file...
  */

  cupsFilePuts(fp, "*PPD-Adobe: \"4.3\"\n");
  cupsFilePuts(fp, "*FormatVersion: \"4.3\"\n");
  cupsFilePrintf(fp, "*FileVersion: \"%d.%d\"\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*LanguageVersion: English\n");
  cupsFilePuts(fp, "*LanguageEncoding: ISOLatin1\n");
  cupsFilePuts(fp, "*PSVersion: \"(3010.000) 0\"\n");
  cupsFilePuts(fp, "*LanguageLevel: \"3\"\n");
  cupsFilePuts(fp, "*FileSystem: False\n");
  cupsFilePuts(fp, "*PCFileName: \"ippeve.ppd\"\n");

  if ((attr = ippFindAttribute(response, "printer-make-and-model", IPP_TAG_TEXT)) != NULL)
    strlcpy(make, ippGetString(attr, 0, NULL), sizeof(make));
  else if (make_model && make_model[0] != '\0')
    strlcpy(make, make_model, sizeof(make));
  else
    strlcpy(make, "Unknown Printer", sizeof(make));

  if (!_cups_strncasecmp(make, "Hewlett Packard ", 16) ||
      !_cups_strncasecmp(make, "Hewlett-Packard ", 16))
  {
    model = make + 16;
    strlcpy(make, "HP", sizeof(make));
  }
  else if ((model = strchr(make, ' ')) != NULL)
    *model++ = '\0';
  else
    model = make;

  cupsFilePrintf(fp, "*Manufacturer: \"%s\"\n", make);
  cupsFilePrintf(fp, "*ModelName: \"%s %s\"\n", make, model);
  cupsFilePrintf(fp, "*Product: \"(%s %s)\"\n", make, model);
  cupsFilePrintf(fp, "*NickName: \"%s %s, driverless, cups-filters %s\"\n", make, model,
		 VERSION);
  cupsFilePrintf(fp, "*ShortNickName: \"%s %s\"\n", make, model);

  if (((attr = ippFindAttribute(response, "color-supported", IPP_TAG_BOOLEAN)) != NULL && ippGetBoolean(attr, 0)) || color)
    cupsFilePuts(fp, "*ColorDevice: True\n");
  else
    cupsFilePuts(fp, "*ColorDevice: False\n");

  cupsFilePrintf(fp, "*cupsVersion: %d.%d\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*cupsSNMPSupplies: False\n");
  cupsFilePuts(fp, "*cupsLanguages: \"en\"\n");

 /*
  * Filters...
  */

  int formatfound = 0;
  if (((attr = ippFindAttribute(response, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL) || (pdl && pdl[0] != '\0'))
  {
    const char *format = pdl;
    i = 0;
    count = ippGetCount(attr);
    while ((attr && i < count) || /* Go through formats in attribute */
	   (!attr && pdl && pdl[0] != '\0' && format[0] != '\0'))
                     /* Go through formats in pdl string (from DNS-SD record) */
    {
      /* Pick next format from attribute */
      if (attr) format = ippGetString(attr, i, NULL);
      if (!_cups_strncasecmp(format, "application/pdf", 15)) {
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 0 -\"\n");
	formatfound = 1;
      } else if (!_cups_strncasecmp(format, "application/postscript", 22)) {
	/* We put a high cost factor here as if a printer supports also
	   another format, like PWG or Apple Raster, we prefer it, as many
	   PostScript printers have bugs in their PostScript interpreters */
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-postscript application/postscript 500 -\"\n");
	formatfound = 1;
      } else if (!_cups_strncasecmp(format, "application/vnd.hp-pclxl", 24)) {
	/* Check whether the gstopxl filter is installed,
	   otherwise ignore the PCL-XL support of the printer */
	if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
	  cups_serverbin = CUPS_SERVERBIN;
	snprintf(filter_path, sizeof(filter_path), "%s/filter/gstopxl",
		 cups_serverbin);
	if (access(filter_path, X_OK) == 0) {
	  /* We put a high cost factor here as if a printer supports also
	     another format, like PWG or Apple Raster, we prefer it, as some
	     PCL-XL printers have bugs in their PCL-XL interpreters */
	  cupsFilePrintf(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/vnd.hp-pclxl 300 gstopxl\"\n");
	  formatfound = 1;
	}
      } else if (!_cups_strncasecmp(format, "application/vnd.hp-pcl", 22)) {
	/* We put a high cost factor here as if a printer supports also
	   another format, like PWG or Apple Raster, we prefer it, as there
	   are some printers, like HP inkjets which report to accept PCL
           but do not support PCL 5c/e or PCL-XL */
        cupsFilePrintf(fp, "*cupsFilter2: \"application/vnd.cups-raster application/vnd.hp-pcl 700 rastertopclx\"\n");
	formatfound = 1;
      } else if (!_cups_strncasecmp(format, "image/pwg-raster", 16)) {
        cupsFilePuts(fp, "*cupsFilter2: \"image/pwg-raster image/pwg-raster 0 -\"\n");
	formatfound = 1;
	is_pwg = 1;
#ifdef CUPS_RASTER_HAVE_APPLERASTER
      } else if (!_cups_strncasecmp(format, "image/urf", 9)) {
        cupsFilePuts(fp, "*cupsFilter2: \"image/urf image/urf 100 -\"\n");
	formatfound = 1;
	is_apple = 1;
#endif
      } else if (!_cups_strncasecmp(format, "image/jpeg", 10)) {
	cupsFilePuts(fp, "*cupsFilter2: \"image/jpeg image/jpeg 0 -\"\n");
	formatfound = 1;
      }
      if (attr)
	/* Next format in attribute */
	i ++;
      else {
	/* Find the next format in the string pdl, if there is none left,
	   go to the terminating zero */
	while (!isspace(*format) && *format != ',' && *format != '\0')
	  format ++;
	while ((isspace(*format) || *format == ',') && *format != '\0')
	  format ++;
      }
    }
  }
  if (formatfound == 0) {
    snprintf(ppdgenerator_msg, sizeof(ppdgenerator_msg),
	     "No data format suitable for PPD auto-generation supported by the printer, not generating PPD file.");
    goto bad_ppd;
  }

 /*
  * PageSize/PageRegion/ImageableArea/PaperDimension
  */

  if ((attr = ippFindAttribute(response, "media-bottom-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, bottom = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > bottom)
        bottom = ippGetInteger(attr, i);
  }
  else
    bottom = 1270;

  if ((attr = ippFindAttribute(response, "media-left-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, left = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > left)
        left = ippGetInteger(attr, i);
  }
  else
    left = 635;

  if ((attr = ippFindAttribute(response, "media-right-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, right = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > right)
        right = ippGetInteger(attr, i);
  }
  else
    right = 635;

  if ((attr = ippFindAttribute(response, "media-top-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, top = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > top)
        top = ippGetInteger(attr, i);
  }
  else
    top = 1270;

  if ((defattr = ippFindAttribute(response, "media-col-default", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-size", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      media_size = ippGetCollection(attr, 0);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim && (pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0))) != NULL)
	strlcpy(ppdname, pwg->ppd, sizeof(ppdname));
      else
	strlcpy(ppdname, "Unknown", sizeof(ppdname));
    }
    else
      strlcpy(ppdname, "Unknown", sizeof(ppdname));
  }
  else if ((pwg = pwgMediaForPWG(ippGetString(ippFindAttribute(response, "media-default", IPP_TAG_ZERO), 0, NULL))) != NULL)
    strlcpy(ppdname, pwg->ppd, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) == NULL)
    attr = ippFindAttribute(response, "media-supported", IPP_TAG_ZERO);
  if (attr && ippGetCount(attr) > 0)
  {
    cupsFilePrintf(fp, "*OpenUI *PageSize: PickOne\n"
		       "*OrderDependency: 10 AnySetup *PageSize\n"
                       "*DefaultPageSize: %s\n", ppdname);

    sizes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0,
			  (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      if (ippGetValueTag(attr) == IPP_TAG_BEGIN_COLLECTION)
      {
	media_size = ippGetCollection(attr, i);
	x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
	y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

	pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));
      }
      else
        pwg = pwgMediaForPWG(ippGetString(attr, i, NULL));

      if (pwg)
      {
        char	twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        if (cupsArrayFind(sizes, (void *)pwg->ppd))
        {
          cupsFilePrintf(fp, "*%% warning: Duplicate size '%s' reported by printer.\n",
			 pwg->ppd);
          continue;
        }

        cupsArrayAdd(sizes, (void *)pwg->ppd);

        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*PageSize %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", pwg->ppd, twidth, tlength);
      }
    }
    cupsFilePuts(fp, "*CloseUI: *PageSize\n");

    cupsArrayDelete(sizes);
    sizes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0,
			  (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    cupsFilePrintf(fp, "*OpenUI *PageRegion: PickOne\n"
                       "*OrderDependency: 10 AnySetup *PageRegion\n"
                       "*DefaultPageRegion: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      if (ippGetValueTag(attr) == IPP_TAG_BEGIN_COLLECTION)
      {
	media_size = ippGetCollection(attr, i);
	x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
	y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

	pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));
      }
      else
        pwg = pwgMediaForPWG(ippGetString(attr, i, NULL));

      if (pwg)
      {
        char	twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        if (cupsArrayFind(sizes, (void *)pwg->ppd))
          continue;

        cupsArrayAdd(sizes, (void *)pwg->ppd);

        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*PageRegion %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", pwg->ppd, twidth, tlength);
      }
    }
    cupsFilePuts(fp, "*CloseUI: *PageRegion\n");

    cupsArrayDelete(sizes);
    sizes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0,
			  (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    cupsFilePrintf(fp, "*DefaultImageableArea: %s\n"
		       "*DefaultPaperDimension: %s\n", ppdname, ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      if (ippGetValueTag(attr) == IPP_TAG_BEGIN_COLLECTION)
      {
	media_size = ippGetCollection(attr, i);
	x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
	y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

	pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));
      }
      else
        pwg = pwgMediaForPWG(ippGetString(attr, i, NULL));

      if (pwg)
      {
        char	tleft[256],		/* Left string */
		tbottom[256],		/* Bottom string */
		tright[256],		/* Right string */
		ttop[256],		/* Top string */
		twidth[256],		/* Width string */
		tlength[256];		/* Length string */

        if (cupsArrayFind(sizes, (void *)pwg->ppd))
          continue;

        cupsArrayAdd(sizes, (void *)pwg->ppd);

        _cupsStrFormatd(tleft, tleft + sizeof(tleft), left * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tbottom, tbottom + sizeof(tbottom), bottom * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tright, tright + sizeof(tright), (pwg->width - right) * 72.0 / 2540.0, loc);
        _cupsStrFormatd(ttop, ttop + sizeof(ttop), (pwg->length - top) * 72.0 / 2540.0, loc);
        _cupsStrFormatd(twidth, twidth + sizeof(twidth), pwg->width * 72.0 / 2540.0, loc);
        _cupsStrFormatd(tlength, tlength + sizeof(tlength), pwg->length * 72.0 / 2540.0, loc);

        cupsFilePrintf(fp, "*ImageableArea %s: \"%s %s %s %s\"\n", pwg->ppd, tleft, tbottom, tright, ttop);
        cupsFilePrintf(fp, "*PaperDimension %s: \"%s %s\"\n", pwg->ppd, twidth, tlength);
      }
    }
    cupsArrayDelete(sizes);
  } else {
    cupsFilePrintf(fp,
		   "*%% Printer did not supply page size info via IPP, using defaults\n"
		   "*OpenUI *PageSize: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageSize\n"
		   "*DefaultPageSize: Letter\n"
		   "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
		   "*PageSize Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
		   "*PageSize Executive/Executive: \"<</PageSize[522 756]>>setpagedevice\"\n"
		   "*PageSize Tabloid/Tabloid: \"<</PageSize[792 1224]>>setpagedevice\"\n"
		   "*PageSize A3/A3: \"<</PageSize[842 1191]>>setpagedevice\"\n"
		   "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
		   "*PageSize A5/A5: \"<</PageSize[420 595]>>setpagedevice\"\n"
		   "*PageSize B5/JIS B5: \"<</PageSize[516 729]>>setpagedevice\"\n"
		   "*PageSize EnvISOB5/Envelope B5: \"<</PageSize[499 709]>>setpagedevice\"\n"
		   "*PageSize Env10/Envelope #10 : \"<</PageSize[297 684]>>setpagedevice\"\n"
		   "*PageSize EnvC5/Envelope C5: \"<</PageSize[459 649]>>setpagedevice\"\n"
		   "*PageSize EnvDL/Envelope DL: \"<</PageSize[312 624]>>setpagedevice\"\n"
		   "*PageSize EnvMonarch/Envelope Monarch: \"<</PageSize[279 540]>>setpagedevice\"\n"
		   "*CloseUI: *PageSize\n"
		   "*OpenUI *PageRegion: PickOne\n"
		   "*OrderDependency: 10 AnySetup *PageRegion\n"
		   "*DefaultPageRegion: Letter\n"
		   "*PageRegion Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
		   "*PageRegion Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
		   "*PageRegion Executive/Executive: \"<</PageSize[522 756]>>setpagedevice\"\n"
		   "*PageRegion Tabloid/Tabloid: \"<</PageSize[792 1224]>>setpagedevice\"\n"
		   "*PageRegion A3/A3: \"<</PageSize[842 1191]>>setpagedevice\"\n"
		   "*PageRegion A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
		   "*PageRegion A5/A5: \"<</PageSize[420 595]>>setpagedevice\"\n"
		   "*PageRegion B5/JIS B5: \"<</PageSize[516 729]>>setpagedevice\"\n"
		   "*PageRegion EnvISOB5/Envelope B5: \"<</PageSize[499 709]>>setpagedevice\"\n"
		   "*PageRegion Env10/Envelope #10 : \"<</PageSize[297 684]>>setpagedevice\"\n"
		   "*PageRegion EnvC5/Envelope C5: \"<</PageSize[459 649]>>setpagedevice\"\n"
		   "*PageRegion EnvDL/Envelope DL: \"<</PageSize[312 624]>>setpagedevice\"\n"
		   "*PageRegion EnvMonarch/Envelope Monarch: \"<</PageSize[279 540]>>setpagedevice\"\n"
		   "*CloseUI: *PageSize\n"
		   "*DefaultImageableArea: Letter\n"
		   "*ImageableArea Letter/US Letter: \"18 12 594 780\"\n"
		   "*ImageableArea Legal/US Legal: \"18 12 594 996\"\n"
		   "*ImageableArea Executive/Executive: \"18 12 504 744\"\n"
		   "*ImageableArea Tabloid/Tabloid: \"18 12 774 1212\"\n"
		   "*ImageableArea A3/A3: \"18 12 824 1179\"\n"
		   "*ImageableArea A4/A4: \"18 12 577 830\"\n"
		   "*ImageableArea A5/A5: \"18 12 402 583\"\n"
		   "*ImageableArea B5/JIS B5: \"18 12 498 717\"\n"
		   "*ImageableArea EnvISOB5/Envelope B5: \"18 12 481 697\"\n"
		   "*ImageableArea Env10/Envelope #10 : \"18 12 279 672\"\n"
		   "*ImageableArea EnvC5/Envelope C5: \"18 12 441 637\"\n"
		   "*ImageableArea EnvDL/Envelope DL: \"18 12 294 612\"\n"
		   "*ImageableArea EnvMonarch/Envelope Monarch: \"18 12 261 528\"\n"
		   "*DefaultPaperDimension: Letter\n"
		   "*PaperDimension Letter/US Letter: \"612 792\"\n"
		   "*PaperDimension Legal/US Legal: \"612 1008\"\n"
		   "*PaperDimension Executive/Executive: \"522 756\"\n"
		   "*PaperDimension Tabloid/Tabloid: \"792 1224\"\n"
		   "*PaperDimension A3/A3: \"842 1191\"\n"
		   "*PaperDimension A4/A4: \"595 842\"\n"
		   "*PaperDimension A5/A5: \"420 595\"\n"
		   "*PaperDimension B5/JIS B5: \"516 729\"\n"
		   "*PaperDimension EnvISOB5/Envelope B5: \"499 709\"\n"
		   "*PaperDimension Env10/Envelope #10 : \"297 684\"\n"
		   "*PaperDimension EnvC5/Envelope C5: \"459 649\"\n"
		   "*PaperDimension EnvDL/Envelope DL: \"312 624\"\n"
		   "*PaperDimension EnvMonarch/Envelope Monarch: \"279 540\"\n");
  }

 /*
  * InputSlot...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-source", IPP_TAG_KEYWORD)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-source-supported", IPP_TAG_KEYWORD)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    static const char * const sources[][2] =
    {					/* "media-source" strings */
      { "Auto", _("Automatic") },
      { "Main", _("Main") },
      { "Alternate", _("Alternate") },
      { "LargeCapacity", _("Large Capacity") },
      { "Manual", _("Manual") },
      { "Envelope", _("Envelope") },
      { "Disc", _("Disc") },
      { "Photo", _("Photo") },
      { "Hagaki", _("Hagaki") },
      { "MainRoll", _("Main Roll") },
      { "AlternateRoll", _("Alternate Roll") },
      { "Top", _("Top") },
      { "Middle", _("Middle") },
      { "Bottom", _("Bottom") },
      { "Side", _("Side") },
      { "Left", _("Left") },
      { "Right", _("Right") },
      { "Center", _("Center") },
      { "Rear", _("Rear") },
      { "ByPassTray", _("Multipurpose") },
      { "Tray1", _("Tray 1") },
      { "Tray2", _("Tray 2") },
      { "Tray3", _("Tray 3") },
      { "Tray4", _("Tray 4") },
      { "Tray5", _("Tray 5") },
      { "Tray6", _("Tray 6") },
      { "Tray7", _("Tray 7") },
      { "Tray8", _("Tray 8") },
      { "Tray9", _("Tray 9") },
      { "Tray10", _("Tray 10") },
      { "Tray11", _("Tray 11") },
      { "Tray12", _("Tray 12") },
      { "Tray13", _("Tray 13") },
      { "Tray14", _("Tray 14") },
      { "Tray15", _("Tray 15") },
      { "Tray16", _("Tray 16") },
      { "Tray17", _("Tray 17") },
      { "Tray18", _("Tray 18") },
      { "Tray19", _("Tray 19") },
      { "Tray20", _("Tray 20") },
      { "Roll1", _("Roll 1") },
      { "Roll2", _("Roll 2") },
      { "Roll3", _("Roll 3") },
      { "Roll4", _("Roll 4") },
      { "Roll5", _("Roll 5") },
      { "Roll6", _("Roll 6") },
      { "Roll7", _("Roll 7") },
      { "Roll8", _("Roll 8") },
      { "Roll9", _("Roll 9") },
      { "Roll10", _("Roll 10") }
    };

    cupsFilePrintf(fp, "*OpenUI *InputSlot: PickOne\n"
                       "*OrderDependency: 10 AnySetup *InputSlot\n"
                       "*DefaultInputSlot: %s\n", ppdname);
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      pwg_ppdize_name(ippGetString(attr, i, NULL), ppdname, sizeof(ppdname));

      for (j = 0; j < (int)(sizeof(sources) / sizeof(sources[0])); j ++)
        if (!strcmp(sources[j][0], ppdname))
	{
	  cupsFilePrintf(fp, "*InputSlot %s/%s: \"<</MediaPosition %d>>setpagedevice\"\n", ppdname, _cupsLangString(lang, sources[j][1]), j);
	  break;
	}
    }
    cupsFilePuts(fp, "*CloseUI: *InputSlot\n");
  }

 /*
  * MediaType...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-type", IPP_TAG_KEYWORD)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "media-type-supported", IPP_TAG_KEYWORD)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    static const char * const media_types[][2] =
    {					/* "media-type" strings */
      { "aluminum", _("Aluminum") },
      { "auto", _("Automatic") },
      { "back-print-film", _("Back Print Film") },
      { "cardboard", _("Cardboard") },
      { "cardstock", _("Cardstock") },
      { "cd", _("CD") },
      { "continuous", _("Continuous") },
      { "continuous-long", _("Continuous Long") },
      { "continuous-short", _("Continuous Short") },
      { "disc", _("Optical Disc") },
      { "disc-glossy", _("Glossy Optical Disc") },
      { "disc-high-gloss", _("High Gloss Optical Disc") },
      { "disc-matte", _("Matte Optical Disc") },
      { "disc-satin", _("Satin Optical Disc") },
      { "disc-semi-gloss", _("Semi-Gloss Optical Disc") },
      { "double-wall", _("Double Wall Cardboard") },
      { "dry-film", _("Dry Film") },
      { "dvd", _("DVD") },
      { "embossing-foil", _("Embossing Foil") },
      { "end-board", _("End Board") },
      { "envelope", _("Envelope") },
      { "envelope-archival", _("Archival Envelope") },
      { "envelope-bond", _("Bond Envelope") },
      { "envelope-coated", _("Coated Envelope") },
      { "envelope-cotton", _("Cotton Envelope") },
      { "envelope-fine", _("Fine Envelope") },
      { "envelope-heavyweight", _("Heavyweight Envelope") },
      { "envelope-inkjet", _("Inkjet Envelope") },
      { "envelope-lightweight", _("Lightweight Envelope") },
      { "envelope-plain", _("Plain Envelope") },
      { "envelope-preprinted", _("Preprinted Envelope") },
      { "envelope-window", _("Windowed Envelope") },
      { "fabric", _("Fabric") },
      { "fabric-archival", _("Archival Fabric") },
      { "fabric-glossy", _("Glossy Fabric") },
      { "fabric-high-gloss", _("High Gloss Fabric") },
      { "fabric-matte", _("Matte Fabric") },
      { "fabric-semi-gloss", _("Semi-Gloss Fabric") },
      { "fabric-waterproof", _("Waterproof Fabric") },
      { "film", _("Film") },
      { "flexo-base", _("Flexo Base") },
      { "flexo-photo-polymer", _("Flexo Photo Polymer") },
      { "flute", _("Flute") },
      { "foil", _("Foil") },
      { "full-cut-tabs", _("Full Cut Tabs") },
      { "glass", _("Glass") },
      { "glass-colored", _("Glass Colored") },
      { "glass-opaque", _("Glass Opaque") },
      { "glass-surfaced", _("Glass Surfaced") },
      { "glass-textured", _("Glass Textured") },
      { "gravure-cylinder", _("Gravure Cylinder") },
      { "image-setter-paper", _("Image Setter Paper") },
      { "imaging-cylinder", _("Imaging Cylinder") },
      { "labels", _("Labels") },
      { "labels-colored", _("Colored Labels") },
      { "labels-glossy", _("Glossy Labels") },
      { "labels-high-gloss", _("High Gloss Labels") },
      { "labels-inkjet", _("Inkjet Labels") },
      { "labels-matte", _("Matte Labels") },
      { "labels-permanent", _("Permanent Labels") },
      { "labels-satin", _("Satin Labels") },
      { "labels-security", _("Security Labels") },
      { "labels-semi-gloss", _("Semi-Gloss Labels") },
      { "laminating-foil", _("Laminating Foil") },
      { "letterhead", _("Letterhead") },
      { "metal", _("Metal") },
      { "metal-glossy", _("Metal Glossy") },
      { "metal-high-gloss", _("Metal High Gloss") },
      { "metal-matte", _("Metal Matte") },
      { "metal-satin", _("Metal Satin") },
      { "metal-semi-gloss", _("Metal Semi Gloss") },
      { "mounting-tape", _("Mounting Tape") },
      { "multi-layer", _("Multi Layer") },
      { "multi-part-form", _("Multi Part Form") },
      { "other", _("Other") },
      { "paper", _("Paper") },
      { "photographic", _("Photo Paper") },
      { "photographic-archival", _("Photographic Archival") },
      { "photographic-film", _("Photo Film") },
      { "photographic-glossy", _("Glossy Photo Paper") },
      { "photographic-high-gloss", _("High Gloss Photo Paper") },
      { "photographic-matte", _("Matte Photo Paper") },
      { "photographic-satin", _("Satin Photo Paper") },
      { "photographic-semi-gloss", _("Semi-Gloss Photo Paper") },
      { "plastic", _("Plastic") },
      { "plastic-archival", _("Plastic Archival") },
      { "plastic-colored", _("Plastic Colored") },
      { "plastic-glossy", _("Plastic Glossy") },
      { "plastic-high-gloss", _("Plastic High Gloss") },
      { "plastic-matte", _("Plastic Matte") },
      { "plastic-satin", _("Plastic Satin") },
      { "plastic-semi-gloss", _("Plastic Semi Gloss") },
      { "plate", _("Plate") },
      { "polyester", _("Polyester") },
      { "pre-cut-tabs", _("Pre Cut Tabs") },
      { "roll", _("Roll") },
      { "screen", _("Screen") },
      { "screen-paged", _("Screen Paged") },
      { "self-adhesive", _("Self Adhesive") },
      { "self-adhesive-film", _("Self Adhesive Film") },
      { "shrink-foil", _("Shrink Foil") },
      { "single-face", _("Single Face") },
      { "single-wall", _("Single Wall Cardboard") },
      { "sleeve", _("Sleeve") },
      { "stationery", _("Stationery") },
      { "stationery-archival", _("Stationery Archival") },
      { "stationery-coated", _("Coated Paper") },
      { "stationery-cotton", _("Stationery Cotton") },
      { "stationery-fine", _("Vellum Paper") },
      { "stationery-heavyweight", _("Heavyweight Paper") },
      { "stationery-heavyweight-coated", _("Stationery Heavyweight Coated") },
      { "stationery-inkjet", _("Stationery Inkjet Paper") },
      { "stationery-letterhead", _("Letterhead") },
      { "stationery-lightweight", _("Lightweight Paper") },
      { "stationery-preprinted", _("Preprinted Paper") },
      { "stationery-prepunched", _("Punched Paper") },
      { "tab-stock", _("Tab Stock") },
      { "tractor", _("Tractor") },
      { "transfer", _("Transfer") },
      { "transparency", _("Transparency") },
      { "triple-wall", _("Triple Wall Cardboard") },
      { "wet-film", _("Wet Film") }
    };

    cupsFilePrintf(fp, "*OpenUI *MediaType: PickOne\n"
                       "*OrderDependency: 10 AnySetup *MediaType\n"
                       "*DefaultMediaType: %s\n", ppdname);
    for (i = 0; i < (int)(sizeof(media_types) / sizeof(media_types[0])); i ++)
    {
      if (!ippContainsString(attr, media_types[i][0]))
        continue;

      pwg_ppdize_name(media_types[i][0], ppdname, sizeof(ppdname));

      cupsFilePrintf(fp, "*MediaType %s/%s: \"<</MediaType(%s)>>setpagedevice\"\n", ppdname, _cupsLangString(lang, media_types[i][1]), ppdname);
    }
    cupsFilePuts(fp, "*CloseUI: *MediaType\n");
  }

 /*
  * ColorModel...
  */

  if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
    if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) == NULL)
      if ((attr = ippFindAttribute(response, "print-color-mode-supported", IPP_TAG_KEYWORD)) == NULL)
        attr = ippFindAttribute(response, "output-mode-supported", IPP_TAG_KEYWORD);

  if (attr && ippGetCount(attr) > 0)
  {
    const char *default_color = NULL;	/* Default */

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *keyword = ippGetString(attr, i, NULL);
					/* Keyword for color/bit depth */

      if (!strcmp(keyword, "black_1") || !strcmp(keyword, "bi-level") || !strcmp(keyword, "process-bi-level"))
      {
        if (!default_color)
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			     "*OrderDependency: 10 AnySetup *ColorModel\n", _cupsLangString(lang, _("Color Mode")));

        cupsFilePrintf(fp, "*ColorModel FastGray/%s: \"<</cupsColorSpace 3/cupsBitsPerColor 1/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n", _cupsLangString(lang, _("Fast Grayscale")));

        if (!default_color)
	  default_color = "FastGray";
      }
      else if (!strcmp(keyword, "sgray_8") || !strcmp(keyword, "W8") || !strcmp(keyword, "monochrome") || !strcmp(keyword, "process-monochrome"))
      {
        if (!default_color)
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			     "*OrderDependency: 10 AnySetup *ColorModel\n", _cupsLangString(lang, _("Color Mode")));

        cupsFilePrintf(fp, "*ColorModel Gray/%s: \"<</cupsColorSpace 18/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n", _cupsLangString(lang, _("Grayscale")));

        if (!default_color || !strcmp(default_color, "FastGray"))
	  default_color = "Gray";
      }
      else if (!strcmp(keyword, "srgb_8") || !strcmp(keyword, "SRGB24") || !strcmp(keyword, "color"))
      {
        if (!default_color)
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			     "*OrderDependency: 10 AnySetup *ColorModel\n", _cupsLangString(lang, _("Color Mode")));

        cupsFilePrintf(fp, "*ColorModel RGB/%s: \"<</cupsColorSpace 19/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n", _cupsLangString(lang, _("Color")));

	default_color = "RGB";
      }
      else if (!strcmp(keyword, "adobe-rgb_16") || !strcmp(keyword, "ADOBERGB48"))
      {
        if (!default_color)
	  cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
			     "*OrderDependency: 10 AnySetup *ColorModel\n", _cupsLangString(lang, _("Color Mode")));

        cupsFilePrintf(fp, "*ColorModel AdobeRGB/%s: \"<</cupsColorSpace 20/cupsBitsPerColor 16/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n", _cupsLangString(lang, _("Deep Color")));

        if (!default_color)
	  default_color = "AdobeRGB";
      }
    }

    if (default_color)
    {
      cupsFilePrintf(fp, "*DefaultColorModel: %s\n", default_color);
      cupsFilePuts(fp, "*CloseUI: *ColorModel\n");
    }
  } else {
    cupsFilePrintf(fp, "*OpenUI *ColorModel/%s: PickOne\n"
		   "*OrderDependency: 10 AnySetup *ColorModel\n", _cupsLangString(lang, _("Color Mode")));
    cupsFilePrintf(fp, "*DefaultColorModel: Gray\n");
    cupsFilePuts(fp, "*ColorModel FastGray/Fast Grayscale: \"<</cupsColorSpace 3/cupsBitsPerColor 1/cupsColorOrder 0/cupsCompression 0/ProcessColorModel /DeviceGray>>setpagedevice\"\n");
    cupsFilePuts(fp, "*ColorModel Gray/Grayscale: \"<</cupsColorSpace 18/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0/ProcessColorModel /DeviceGray>>setpagedevice\"\n");
    if (color) {
      /* Color printer according to DNS-SD (or unknown) */
      cupsFilePuts(fp, "*ColorModel RGB/Color: \"<</cupsColorSpace 19/cupsBitsPerColor 8/cupsColorOrder 0/cupsCompression 0/ProcessColorModel /DeviceRGB>>setpagedevice\"\n");
    }
    cupsFilePuts(fp, "*CloseUI: *ColorModel\n");
  }

 /*
  * Duplex...
  */

  if (((attr = ippFindAttribute(response, "sides-supported",
				IPP_TAG_KEYWORD)) != NULL &&
       ippContainsString(attr, "two-sided-long-edge")) ||
      (attr == NULL && duplex))
  {
    cupsFilePrintf(fp, "*OpenUI *Duplex/%s: PickOne\n"
		       "*OrderDependency: 10 AnySetup *Duplex\n"
		       "*DefaultDuplex: None\n"
		       "*Duplex None/%s: \"<</Duplex false>>setpagedevice\"\n"
		       "*Duplex DuplexNoTumble/%s: \"<</Duplex true/Tumble false>>setpagedevice\"\n"
		       "*Duplex DuplexTumble/%s: \"<</Duplex true/Tumble true>>setpagedevice\"\n"
		       "*CloseUI: *Duplex\n", _cupsLangString(lang, _("2-Sided Printing")), _cupsLangString(lang, _("Off (1-Sided)")), _cupsLangString(lang, _("Long-Edge (Portrait)")), _cupsLangString(lang, _("Short-Edge (Landscape)")));

    if ((attr = ippFindAttribute(response, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) != NULL)
    {
      const char *keyword = ippGetString(attr, 0, NULL);
					/* Keyword value */

      if (!strcmp(keyword, "flipped"))
        cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
      else if (!strcmp(keyword, "manual-tumble"))
        cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
      else if (!strcmp(keyword, "normal"))
        cupsFilePuts(fp, "*cupsBackSide: Normal\n");
      else
        cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
    }
    else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      for (i = 0, count = ippGetCount(attr); i < count; i ++)
      {
	const char *dm = ippGetString(attr, i, NULL);
					  /* DM value */

	if (!_cups_strcasecmp(dm, "DM1"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Normal\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM2"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM3"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
	  break;
	}
	else if (!_cups_strcasecmp(dm, "DM4"))
	{
	  cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
	  break;
	}
      }
    }
  }

 /*
  * Output bin...
  */

  if ((attr = ippFindAttribute(response, "output-bin-default", IPP_TAG_ZERO)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(response, "output-bin-supported", IPP_TAG_ZERO)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    static const char * const output_bins[][2] =
    {					/* "output-bin" strings */
      { "auto", _("Automatic") },
      { "bottom", _("Bottom Tray") },
      { "center", _("Center Tray") },
      { "face-down", _("Face Down") },
      { "face-up", _("Face Up") },
      { "large-capacity", _("Large Capacity Tray") },
      { "left", _("Left Tray") },
      { "mailbox-1", _("Mailbox 1") },
      { "mailbox-2", _("Mailbox 2") },
      { "mailbox-3", _("Mailbox 3") },
      { "mailbox-4", _("Mailbox 4") },
      { "mailbox-5", _("Mailbox 5") },
      { "mailbox-6", _("Mailbox 6") },
      { "mailbox-7", _("Mailbox 7") },
      { "mailbox-8", _("Mailbox 8") },
      { "mailbox-9", _("Mailbox 9") },
      { "mailbox-10", _("Mailbox 10") },
      { "middle", _("Middle") },
      { "my-mailbox", _("My Mailbox") },
      { "rear", _("Rear Tray") },
      { "right", _("Right Tray") },
      { "side", _("Side Tray") },
      { "stacker-1", _("Stacker 1") },
      { "stacker-2", _("Stacker 2") },
      { "stacker-3", _("Stacker 3") },
      { "stacker-4", _("Stacker 4") },
      { "stacker-5", _("Stacker 5") },
      { "stacker-6", _("Stacker 6") },
      { "stacker-7", _("Stacker 7") },
      { "stacker-8", _("Stacker 8") },
      { "stacker-9", _("Stacker 9") },
      { "stacker-10", _("Stacker 10") },
      { "top", _("Top Tray") },
      { "tray-1", _("Tray 1") },
      { "tray-2", _("Tray 2") },
      { "tray-3", _("Tray 3") },
      { "tray-4", _("Tray 4") },
      { "tray-5", _("Tray 5") },
      { "tray-6", _("Tray 6") },
      { "tray-7", _("Tray 7") },
      { "tray-8", _("Tray 8") },
      { "tray-9", _("Tray 9") },
      { "tray-10", _("Tray 10") }
    };

    cupsFilePrintf(fp, "*OpenUI *OutputBin: PickOne\n"
                       "*OrderDependency: 10 AnySetup *OutputBin\n"
                       "*DefaultOutputBin: %s\n", ppdname);
    for (i = 0; i < (int)(sizeof(output_bins) / sizeof(output_bins[0])); i ++)
    {
      if (!ippContainsString(attr, output_bins[i][0]))
        continue;

      pwg_ppdize_name(output_bins[i][0], ppdname, sizeof(ppdname));

      cupsFilePrintf(fp, "*OutputBin %s/%s: \"\"\n", ppdname, _cupsLangString(lang, output_bins[i][1]));
    }
    cupsFilePuts(fp, "*CloseUI: *OutputBin\n");
  }

 /*
  * Finishing options...
  */

  if ((attr = ippFindAttribute(response, "finishings-col-database", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    ipp_t		*col;		/* Collection value */
    ipp_attribute_t	*template;	/* "finishing-template" member */
    const char		*name;		/* String name */
    int			value;		/* Enum value, if any */
    cups_array_t	*names;		/* Names we've added */

    count = ippGetCount(attr);
    names = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    cupsFilePrintf(fp, "*OpenUI *cupsFinishingTemplate/%s: PickMany\n"
		       "*OrderDependency: 10 AnySetup *cupsFinishingTemplate\n"
		       "*DefaultcupsFinishingTemplate: none\n"
		       "*cupsFinishingTemplate none/%s: \"\"\n"
		       "*cupsIPPFinishings 3/none: \"*cupsFinishingTemplate none\"\n", _cupsLangString(lang, _("Finishing")), _cupsLangString(lang, _("No Finishing")));

    for (i = 0; i < count; i ++)
    {
      col      = ippGetCollection(attr, i);
      template = ippFindAttribute(col, "finishing-template", IPP_TAG_ZERO);

      if ((name = ippGetString(template, 0, NULL)) == NULL || !strcmp(name, "none"))
        continue;

      if (cupsArrayFind(names, (char *)name))
        continue;			/* Already did this finishing template */

      cupsArrayAdd(names, (char *)name);

      for (j = 0; j < (int)(sizeof(finishings) / sizeof(finishings[0])); j ++)
      {
        if (!strcmp(finishings[j][0], name))
	{
          cupsFilePrintf(fp, "*cupsFinishingTemplate %s/%s: \"\"\n", name, _cupsLangString(lang, finishings[j][1]));

	  value = ippEnumValue("finishings", name);

	  if (value)
	    cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*cupsFinishingTemplate %s\"\n", value, name, name);
          break;
	}
      }
    }

    cupsArrayDelete(names);

    cupsFilePuts(fp, "*CloseUI: *cupsFinishingTemplate\n");
  }
  else if ((attr = ippFindAttribute(response, "finishings-supported", IPP_TAG_ENUM)) != NULL && (count = ippGetCount(attr)) > 1 )
  {
    const char		*name;		/* String name */
    int			value;		/* Enum value, if any */

    count = ippGetCount(attr);

    cupsFilePrintf(fp, "*OpenUI *cupsFinishingTemplate/%s: PickMany\n"
		       "*OrderDependency: 10 AnySetup *cupsFinishingTemplate\n"
		       "*DefaultcupsFinishingTemplate: none\n"
		       "*cupsFinishingTemplate none/%s: \"\"\n"
		       "*cupsIPPFinishings 3/none: \"*cupsFinishingTemplate none\"\n", _cupsLangString(lang, _("Finishing")), _cupsLangString(lang, _("No Finishing")));

    for (i = 0; i < count; i ++)
    {
      if ((value = ippGetInteger(attr, i)) == 3)
        continue;

      name = ippEnumString("finishings", value);
      for (j = 0; j < (int)(sizeof(finishings) / sizeof(finishings[0])); j ++)
      {
        if (!strcmp(finishings[j][0], name))
	{
          cupsFilePrintf(fp, "*cupsFinishingTemplate %s/%s: \"\"\n", name, _cupsLangString(lang, finishings[j][1]));
	  cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*cupsFinishingTemplate %s\"\n", value, name, name);
          break;
	}
      }
    }

    cupsFilePuts(fp, "*CloseUI: *cupsFinishingTemplate\n");
  }

 /*
  * cupsPrintQuality and DefaultResolution...
  */

  if ((attr = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
  {
    count = ippGetCount(attr);

    pwg_ppdize_resolution(attr, count / 2, &xres, &yres, ppdname, sizeof(ppdname));
    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);

    cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality/%s: PickOne\n"
		       "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
		       "*DefaultcupsPrintQuality: Normal\n", _cupsLangString(lang, _("Print Quality")));
    if (count > 2)
    {
      pwg_ppdize_resolution(attr, 0, &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality Draft/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("Draft")), xres, yres);
    }
    pwg_ppdize_resolution(attr, count / 2, &xres, &yres, NULL, 0);
    cupsFilePrintf(fp, "*cupsPrintQuality Normal/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("Normal")), xres, yres);
    if (count > 1)
    {
      pwg_ppdize_resolution(attr, count - 1, &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality High/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("High")), xres, yres);
    }

    cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
  }
  else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    int lowdpi = 0, hidpi = 0;		/* Lower and higher resolution */

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *rs = ippGetString(attr, i, NULL);
					/* RS value */

      if (_cups_strncasecmp(rs, "RS", 2))
        continue;

      lowdpi = atoi(rs + 2);
      if ((rs = strrchr(rs, '-')) != NULL)
        hidpi = atoi(rs + 1);
      else
        hidpi = lowdpi;
      break;
    }

    if (lowdpi == 0)
    {
     /*
      * Invalid "urf-supported" value...
      */

      snprintf(ppdgenerator_msg, sizeof(ppdgenerator_msg),
	       "Invalid resolution entry in \"urf-supported\" IPP attribute: %s. Not generating PPD file.",
	       ippGetString(attr, i, NULL));
	       goto bad_ppd;
    }
    else
    {
     /*
      * Generate print qualities based on low and high DPIs...
      */

      cupsFilePrintf(fp, "*DefaultResolution: %ddpi\n", lowdpi);

      cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality/%s: PickOne\n"
			 "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
			 "*DefaultcupsPrintQuality: Normal\n", _cupsLangString(lang, _("Print Quality")));
      if ((lowdpi & 1) == 0)
	cupsFilePrintf(fp, "*cupsPrintQuality Draft/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("Draft")), lowdpi, lowdpi / 2);
      cupsFilePrintf(fp, "*cupsPrintQuality Normal/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("Normal")), lowdpi, lowdpi);
      if (hidpi > lowdpi)
	cupsFilePrintf(fp, "*cupsPrintQuality High/%s: \"<</HWResolution[%d %d]>>setpagedevice\"\n", _cupsLangString(lang, _("High")), hidpi, hidpi);
      cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
    }
  }
  else if (is_apple || is_pwg) {
    snprintf(ppdgenerator_msg, sizeof(ppdgenerator_msg),
	     "IPP attribute \"%s\" missing, needed for %s Raster output format, not generating PPD file.",
	     (is_pwg ? "pwg-raster-document-resolution-supported" :
	      "urf-supported"),
	     (is_pwg ? "PWG" : "Apple"));
    goto bad_ppd;
  } else if ((attr = ippFindAttribute(response, "printer-resolution-default", IPP_TAG_RESOLUTION)) != NULL)
  {
    pwg_ppdize_resolution(attr, 0, &xres, &yres, ppdname, sizeof(ppdname));
    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);
  }
  else
    cupsFilePuts(fp, "*DefaultResolution: 300dpi\n");

 /*
  * Close up and return...
  */

  snprintf(ppdgenerator_msg, sizeof(ppdgenerator_msg),
	   "%s PPD generated.",
	   (is_pwg ? "IPP Everywhere" :
	    (is_apple ? "Apple Raster" :
	     "Legacy IPP printer")));

  cupsFileClose(fp);

  return (buffer);

 /*
  * If we get here then there was a problem creating the PPD...
  */

  bad_ppd:

  cupsFileClose(fp);
  unlink(buffer);
  *buffer = '\0';

  return (NULL);
}


/*
 * '_pwgInputSlotForSource()' - Get the InputSlot name for the given PWG
 *                              media-source.
 */

const char *				/* O - InputSlot name */
_pwgInputSlotForSource(
    const char *media_source,		/* I - PWG media-source */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_source || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_source, "main"))
    strlcpy(name, "Cassette", namesize);
  else if (_cups_strcasecmp(media_source, "alternate"))
    strlcpy(name, "Multipurpose", namesize);
  else if (_cups_strcasecmp(media_source, "large-capacity"))
    strlcpy(name, "LargeCapacity", namesize);
  else if (_cups_strcasecmp(media_source, "bottom"))
    strlcpy(name, "Lower", namesize);
  else if (_cups_strcasecmp(media_source, "middle"))
    strlcpy(name, "Middle", namesize);
  else if (_cups_strcasecmp(media_source, "top"))
    strlcpy(name, "Upper", namesize);
  else if (_cups_strcasecmp(media_source, "rear"))
    strlcpy(name, "Rear", namesize);
  else if (_cups_strcasecmp(media_source, "side"))
    strlcpy(name, "Side", namesize);
  else if (_cups_strcasecmp(media_source, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_source, "main-roll"))
    strlcpy(name, "Roll", namesize);
  else if (_cups_strcasecmp(media_source, "alternate-roll"))
    strlcpy(name, "Roll2", namesize);
  else
    pwg_ppdize_name(media_source, name, namesize);

  return (name);
}


/*
 * '_pwgMediaTypeForType()' - Get the MediaType name for the given PWG
 *                            media-type.
 */

const char *				/* O - MediaType name */
_pwgMediaTypeForType(
    const char *media_type,		/* I - PWG media-type */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_type || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_type, "auto"))
    strlcpy(name, "Auto", namesize);
  else if (_cups_strcasecmp(media_type, "cardstock"))
    strlcpy(name, "Cardstock", namesize);
  else if (_cups_strcasecmp(media_type, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-glossy"))
    strlcpy(name, "Glossy", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-high-gloss"))
    strlcpy(name, "HighGloss", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-matte"))
    strlcpy(name, "Matte", namesize);
  else if (_cups_strcasecmp(media_type, "stationery"))
    strlcpy(name, "Plain", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-coated"))
    strlcpy(name, "Coated", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-inkjet"))
    strlcpy(name, "Inkjet", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-letterhead"))
    strlcpy(name, "Letterhead", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-preprinted"))
    strlcpy(name, "Preprinted", namesize);
  else if (_cups_strcasecmp(media_type, "transparency"))
    strlcpy(name, "Transparency", namesize);
  else
    pwg_ppdize_name(media_type, name, namesize);

  return (name);
}


/*
 * '_pwgPageSizeForMedia()' - Get the PageSize name for the given media.
 */

const char *				/* O - PageSize name */
_pwgPageSizeForMedia(
    pwg_media_t *media,		/* I - Media */
    char         *name,			/* I - PageSize name buffer */
    size_t       namesize)		/* I - Size of name buffer */
{
  const char	*sizeptr,		/* Pointer to size in PWG name */
		*dimptr;		/* Pointer to dimensions in PWG name */


 /*
  * Range check input...
  */

  if (!media || !name || namesize < PPD_MAX_NAME)
    return (NULL);

 /*
  * Copy or generate a PageSize name...
  */

  if (media->ppd)
  {
   /*
    * Use a standard Adobe name...
    */

    strlcpy(name, media->ppd, namesize);
  }
  else if (!media->pwg || !strncmp(media->pwg, "custom_", 7) ||
           (sizeptr = strchr(media->pwg, '_')) == NULL ||
	   (dimptr = strchr(sizeptr + 1, '_')) == NULL ||
	   (size_t)(dimptr - sizeptr) > namesize)
  {
   /*
    * Use a name of the form "wNNNhNNN"...
    */

    snprintf(name, namesize, "w%dh%d", (int)PWG_TO_POINTS(media->width),
             (int)PWG_TO_POINTS(media->length));
  }
  else
  {
   /*
    * Copy the size name from class_sizename_dimensions...
    */

    memcpy(name, sizeptr + 1, (size_t)(dimptr - sizeptr - 1));
    name[dimptr - sizeptr - 1] = '\0';
  }

  return (name);
}


/*
 * 'pwg_ppdize_name()' - Convert an IPP keyword to a PPD keyword.
 */

static void
pwg_ppdize_name(const char *ipp,	/* I - IPP keyword */
                char       *name,	/* I - Name buffer */
		size_t     namesize)	/* I - Size of name buffer */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */


  *name = (char)toupper(*ipp++);

  for (ptr = name + 1, end = name + namesize - 1; *ipp && ptr < end;)
  {
    if (*ipp == '-' && _cups_isalpha(ipp[1]))
    {
      ipp ++;
      *ptr++ = (char)toupper(*ipp++ & 255);
    }
    else
      *ptr++ = *ipp++;
  }

  *ptr = '\0';
}



/*
 * 'pwg_ppdize_resolution()' - Convert PWG resolution values to PPD values.
 */

static void
pwg_ppdize_resolution(
    ipp_attribute_t *attr,		/* I - Attribute to convert */
    int             element,		/* I - Element to convert */
    int             *xres,		/* O - X resolution in DPI */
    int             *yres,		/* O - Y resolution in DPI */
    char            *name,		/* I - Name buffer */
    size_t          namesize)		/* I - Size of name buffer */
{
  ipp_res_t units;			/* Units for resolution */


  *xres = ippGetResolution(attr, element, yres, &units);

  if (units == IPP_RES_PER_CM)
  {
    *xres = (int)(*xres * 2.54);
    *yres = (int)(*yres * 2.54);
  }

  if (name && namesize > 4)
  {
    if (*xres == *yres)
      snprintf(name, namesize, "%ddpi", *xres);
    else
      snprintf(name, namesize, "%dx%ddpi", *xres, *yres);
  }
}
#endif /* HAVE_CUPS_1_6 */

/*
 * End
 */
